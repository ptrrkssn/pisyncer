#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <locale.h>
#include <netdb.h>
#include <unistd.h>
#include <ftw.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <signal.h>
#include <rpcsvc/ypclnt.h>
#include <rpcsvc/yp_prot.h>

#include "html.h"
#include "form.h"

time_t now;

int admin_f = 0;

char *rootdir = "/var/msync";

char *admins = NULL;   /* user,user,user or @netgroup */

char *remote_user = NULL;
char *remote_addr = NULL;

char *request_uri = NULL;
char *request_url = NULL;


int
is_admin_user(const char *user)
{
    char *buf, *cp;

    
    if (!user)
	return 0;
    
    if (!admins)
	return 1;

    buf = strdup(admins);
    if (!buf)
	return 0;

    cp = strtok(buf, ",");
    while (cp)
    {
	if (*cp == '@')
	{
	    if (innetgr(cp+1, NULL, user, NULL))
	    {
		free(buf);
		return 1;
	    }
	}
	else
	{
	    if (strcmp(cp, user) == 0)
	    {
		free(buf);
		return 1;
	    }
	}

	cp = strtok(NULL, ",");
    }

    free(buf);
    return 0;
}



int
spawnv(const char *path,
       char *const argv[],
       const char *f_stdin,
       const char *f_stdout,
       const char *f_stderr)
{
    int pid;


    if ((pid = fork()) != 0)
	return pid;

    /* --- In child --- */
    umask(0);
  
    if (setsid() < 0)
	_exit(1);
  
    freopen(f_stdin  ? f_stdin  : "/dev/null", "r", stdin);
    freopen(f_stdout ? f_stdout : "/dev/null", "w", stdout);
    freopen(f_stderr ? f_stderr : "/dev/null", "w", stderr);
  
    execv(path, argv);
    _exit(1);
}


static int
del_object(const char *path,
	   const struct stat *sp,
	   int flags,
	   struct FTW *fwp) 
{
    return unlink(path);
}


int
dirdel(const char *path)
{
    return nftw(path, del_object, FTW_DEPTH|FTW_MOUNT|FTW_PHYS, 200);
}


int
start_sync(const char *home,
	   const char *ifm_user,
	   const char *ifm_pass,
	   const char *liu_user,
	   const char *liu_pass,
	   const char *liu_path)
{
    char *argv[10];
    FILE *fp;
    int pid, sv;

  
    if (mkdir(home, 0700) < 0)
	return -1; /* Already running, or could not create temp directory */
  
    if (chdir(home) < 0) {
	(void) unlink(home);
	return -1;
    }
  
    fp = fopen("config", "w");
    if (!fp) {
	chdir("/");
	dirdel(home);
	return -1;
    }
  
    fprintf(fp, "Expunge None\n");
    fprintf(fp, "Create Slave\n");
    fprintf(fp, "\n");
    fprintf(fp, "IMAPStore IFM\n");
    fprintf(fp, "Host mail.ifm.liu.se\n");
    fprintf(fp, "SSLType STARTTLS\n");
    fprintf(fp, "SSLVersions TLSv1.2\n");
    fprintf(fp, "AuthMechs LOGIN\n");
    fprintf(fp, "User %s\n", ifm_user);
    fprintf(fp, "Pass %s\n", ifm_pass);
    fprintf(fp, "\n");
    fprintf(fp, "IMAPStore LiU\n");
    fprintf(fp, "Host mail.liu.se\n");
    fprintf(fp, "SSLType STARTTLS\n");
    fprintf(fp, "SSLVersions TLSv1\n");
    fprintf(fp, "AuthMechs LOGIN\n");
    fprintf(fp, "User %s\n", liu_user);
    fprintf(fp, "Pass %s\n", liu_pass);
    fprintf(fp, "\n");
    fprintf(fp, "Channel IFM2LiU\n");
    fprintf(fp, "Master :IFM:\n");
    fprintf(fp, "Slave :LiU:%s/\n", liu_path);
    fprintf(fp, "Patterns * !Trash\n");
    fprintf(fp, "Sync Pull\n");
    fclose(fp);
  
    argv[0] = "mbsync";
    argv[1] = "-c";
    argv[2] = "config";
    argv[3] = "-a";
    argv[4] = "-V";
    argv[5] = NULL;
  
    if ((pid = fork()) == 0) {
	setenv("HOME", home, 1);
	pid = spawnv("/ifm/bin/mbsync", argv, NULL, "stdout", "stderr");
    
	fp = fopen("pid", "w");
	if (!fp) {
	    kill(pid, SIGTERM);
	    _exit(1);
	}
	fprintf(fp, "%u", pid);
	fclose(fp);
    
	while (waitpid(pid, &sv, 0) < 0 && errno == EINTR)
	    ;
    
	_exit(0);
    }
    
    return 0;
}
    
    
char *
ifm2liuid(const char *ifmid)
{
    char *dom = NULL;
    char *res;
    int reslen;


    if (yp_get_default_domain(&dom) != 0)
	return NULL;

    if (yp_match(dom, "liuid.byname", (char *) ifmid, strlen(ifmid), &res, &reslen) != 0)
	return NULL;

    res[reslen] = '\0';
    return res;
}


int
check_mailbox(char *path, int *nm, int *ts, int *ms)
{
    DIR *dp;
    struct dirent *dep;
    char *cp;

    cp = path+strlen(path);

    strcpy(cp, "/new");
    dp = opendir(path);
    if (!dp)
	return -1;

    while ((dep = readdir(dp)) != NULL) {
	if (strcmp(dep->d_name, ".") == 0 || strcmp(dep->d_name, "..") == 0)
	    continue;

	++*nm;
    }

    closedir(dp);

    strcpy(cp, "/cur");
    dp = opendir(path);
    if (!dp)
	return -1;

    while ((dep = readdir(dp)) != NULL) {
	if (strcmp(dep->d_name, ".") == 0 || strcmp(dep->d_name, "..") == 0)
	    continue;

	++*nm;
    }

    closedir(dp);
    return 0;
}

int
check_maildir(const char *ifmid,
	      int *nf,
	      int *nm,
	      int *tms,
	      int *mms,
	      int *lfm,
	      int *lfs)
{
    char path[2048], *cp;
    DIR *dp;
    struct dirent *dep;
    int n_mail, t_mail_size, l_mail_size;


    strcpy(path, "/home/");
    strcat(path, ifmid);
    strcat(path, "/Maildir");

    dp = opendir(path);
    if (!dp)
	return -1;

    cp = path + strlen(path);

    n_mail = t_mail_size = l_mail_size = 0;

    check_mailbox(path, &n_mail, &t_mail_size, &l_mail_size);
    ++*nf;
    *nm += n_mail;
    *tms += t_mail_size;
    if (l_mail_size > *mms)
	*mms = l_mail_size;
    if (n_mail > *lfm)
	*lfm = n_mail;
    if (t_mail_size > *lfs)
	*lfs = t_mail_size;

    while ((dep = readdir(dp)) != NULL) {
	if (dep->d_name[0] != '.')
	    continue;

	if (strcmp(dep->d_name, ".") == 0 || strcmp(dep->d_name, "..") == 0)
	    continue;

	n_mail = t_mail_size = l_mail_size = 0;
	strcpy(cp, "/");
	strcat(cp, dep->d_name);

	check_mailbox(path, &n_mail, &t_mail_size, &l_mail_size);
	++*nf;
	*nm += n_mail;
	*tms += t_mail_size;
	if (l_mail_size > *mms)
	    *mms = l_mail_size;
	if (n_mail > *lfm)
	    *lfm = n_mail;
	if (t_mail_size > *lfs)
	    *lfs = t_mail_size;
    }

    closedir(dp);
    return *nf;
}

int
main(int argc,
     char *argv[])
{
    int pid, c;
    char *cp, home[2048];
    char *action = NULL;
    char *force = NULL;
    char *ifm_user = NULL;
    char *ifm_pass = NULL;
    char *liu_user = NULL;
    char *liu_pass = NULL;
    char *liu_path = "IFM";
    FILE *fp;
    struct passwd *pp;
    int nf, nm, tms, mms, lfm, lfs;
    
    
    signal(SIGCHLD, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 80);
  
    puts("Content-Type: text/html\n");
  
    time(&now);
    srand(now);
  
    setlocale(LC_ALL, "sv");
  
  
    cp = getenv("ADMINS");
    if (cp)
	admins = strdup(cp);
  
    remote_user = getenv("REDIRECT_REMOTE_USER");
    if (!remote_user)
	remote_user = getenv("REMOTE_USER");
  
    if (remote_user && is_admin_user(remote_user))
	admin_f = 1;
  
  
    remote_addr = getenv("REMOTE_ADDR");
    request_uri = getenv("REQUEST_URI");
    if (request_uri)
    {
	request_url = strdup(request_uri);
	cp = strchr(request_url, '?');
	if (cp)
	    *cp = '\0';
    }
    else
	request_uri = request_url = "?";
  
    if (!remote_addr)
	remote_addr = "unknown";
  

    h_header("MailSync");
    form_init(stdin);

    form_gets("action", &action);
    form_gets("force", &force);

    form_gets("ifmid", &ifm_user);
    form_gets("ifmpass", &ifm_pass);
    form_gets("liuid", &liu_user);
    form_gets("liupass", &liu_pass);
    form_gets("liupath", &liu_path);
    
    if (!admin_f || (ifm_user && strlen(ifm_user) > 64)) {
	ifm_user = remote_user;
	ifm_pass = NULL;
	liu_user = NULL;
	liu_pass = NULL;
	liu_path = "IFM";
    }

    if (!ifm_user)
	ifm_user = remote_user;

    if (ifm_pass && strlen(ifm_pass) > 64)
	ifm_pass = NULL;

    if (liu_user && (strlen(liu_user) > 64 || strlen(liu_user) < 1))
	liu_user = NULL;

    if (!liu_user)
	liu_user = ifm2liuid(ifm_user);

    if (liu_pass && strlen(liu_pass) > 64)
	liu_pass = NULL;

    if (liu_path && (strlen(liu_path) > 128 || strlen(liu_path) < 1))
	liu_path = "IFM";

    pp = getpwnam(ifm_user);
    if (pp) {
	setgid(pp->pw_gid);
	setuid(pp->pw_uid);
    }

    if (!ifm_user || !*ifm_user || !ifm_pass || !*ifm_pass || !liu_user || !*liu_user || !liu_path || !*liu_path) {
    
	puts("This utility can be used to sync IFM mailboxes to the LiU Exchange system.");
	puts("The IFM mail folders will be stored in the specified subfolder on the LiU Exchange system.<p>");

	h_form_start("post", request_url);

	h_table_start("accounts");

	h_tr_start("header");
	h_th_str("IFM Account:", "site", 2);
	h_th_str("LiU Account:", "site", 2);
	h_tr_end();

	h_tr_start("odd");
	h_th_str("Username:", "label", 1);
	h_td_str(ifm_user, "name", 1);
	h_form_str("ifmid", "hidden", 16, ifm_user);
	h_th_str("Username:", "label", 1);
	h_td_start("name", 1);
	h_form_str("liuid", "text", 16, liu_user);
	h_tr_end();

	h_tr_start("odd");
	h_th_str("Password:", "label", 1);
	h_td_start("pass", 1);
	h_form_str("ifmpass", "password", 16, NULL);
	h_th_str("Password:", "label", 1);
	h_td_start("pass", 1);
	h_form_str("liuid", "password", 16, NULL);
	h_tr_end();

	h_tr_start("odd");
	h_th_str("Real name:", "label", 1);
	h_td_str(pp ? pp->pw_gecos : "", "name", 1);
	h_th_str("Subfolder:", "label", 1);
	h_td_start("name", 1);
	h_form_str("liupath", "text", 16, liu_path);
	h_tr_end();
	
	nf = nm = tms = mms = lfm = lfs = 0;

	if (check_maildir(ifm_user, &nf, &nm, &tms, &mms, &lfm, &lfs) > 0) {
	    h_tr_start("odd");
	    h_th_str("# Folders:", "label", 1);
	    h_td_int(nf, "value", 1);
	    h_tr_end();
	    h_tr_start("odd");
	    h_th_str("# Mails:", "label", 1);
	    h_td_int(nm, "value", 1);
	    h_tr_end();
	}

	h_table_end();
	
	h_form_str("action", "hidden", 16, "sync");
	puts("<p>");

	puts("Force restart: ");
	h_checkbox("force", "Force", force ? 1 : 0);

	puts("<p>");
	h_button_submit("action", "Sync",  "Sync");
	h_form_end();
	goto End;
    } 

    sprintf(home, "%s/%s", rootdir, ifm_user);

    printf("Sync mail folders from %s@IFM to %s@LIU/%s", ifm_user, liu_user, liu_path);
    puts("<h3>Status</h3>");
  
    if (chdir(home) < 0 && errno == ENOENT) {
      Start:
	start_sync(home, ifm_user, ifm_pass, liu_user, liu_pass, liu_path);
	sleep(1);
    }
  
    fp = fopen("pid", "r");
    if (fp) {
	if (fscanf(fp, "%u", &pid) == 1) {
	    if (kill(pid, 0) == 0) {
		printf("Sync process running with pid=%u\n", pid);
	    } else {
		if (force) {
		    chdir("/");
		    dirdel(home);
		    goto Start;
		}

		puts("Sync process finished");
	    }
	}
	fclose(fp);
    } else
	puts("Sync process starting...");
  
    fp = fopen("stderr", "r");
    if (fp) {
	puts("<h3>Error Log</h3><pre>");
	while ((c = getc(fp)) != EOF)
	    h_putc(c);
	puts("</pre>");
	fclose(fp);
    }
  
    fp = fopen("stdout", "r");
    if (fp) {
	puts("<h3>Process Log</h3><pre>");
	while ((c = getc(fp)) != EOF)
	    h_putc(c);
	puts("</pre>");
	fclose(fp);
    }

  End:
    h_footer(NULL);
    return 0;
}
    
