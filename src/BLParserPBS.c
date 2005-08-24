#include "BLParserPBS.h"

int main(int argc, char *argv[]) {

    struct    sockaddr_in servaddr;
    char      *endptr;
    int       i;
    int       set = 1;
    int       status;
    int       list_s;
    
    char      eventsfile[MAX_CHARS]="\0";
    
    time_t now;
    struct tm *tptr;
    char cnow[30];
   
    char *szPort;
    char *szSpoolDir;
    
    char *espooldir;

    pthread_t ReadThd[NUMTHRDS];
    pthread_t UpdateThd;

    argv0 = argv[0];

    ParseCmdLine(argc, argv, &szPort, &szSpoolDir);
        
    if((argc > 1) && (szPort!=NULL)){
     port = strtol(szPort, &endptr, 0);
     if ( *endptr || port < 1 || port > 65535) {
       fprintf(stderr,"%s: Invalid port supplied.\n",progname);
       exit(EXIT_FAILURE);
     }
    }else{
     port=DEFAULT_PORT;
    }
        
    /* Get log dir name */
  
    if((ldir=malloc(STR_CHARS)) == 0){
     sysfatal("can't malloc line: %r");
    }
    
    
    if(szSpoolDir!=NULL){
     spooldir=szSpoolDir;
    }else if((espooldir=getenv("PBS_SPOOL_DIR"))!=NULL){
     spooldir=espooldir;
    }
        
    strcat(ldir,spooldir);
    strcat(ldir,"/server_logs");
    
    now=time(NULL);
    tptr=localtime(&now);
    strftime(cnow,sizeof(cnow),"%Y%m%d",tptr);
    
    strcat(eventsfile,ldir);
    strcat(eventsfile,"/");
    strcat(eventsfile,cnow);
    
    /* Get all events from last log */
    
     GetAllEvents(eventsfile);

    /*  Create the listening socket  */

    if ( (list_s = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
	fprintf(stderr, "%s: Error creating listening socket.\n",progname);
	exit(EXIT_FAILURE);
    }

    if(setsockopt(list_s, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0) {
        fprintf(stderr,"%s: setsockopt() failed\n",progname);
    }

    /*  Set all bytes in socket address structure to
        zero, and fill in the relevant data members   */

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(port);


    /*  Bind our socket addresss to the 
	listening socket, and call listen()  */

    if ( bind(list_s, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0 ) {
	fprintf(stderr, "%s: Error calling bind()\n",progname);
	exit(EXIT_FAILURE);
    }
    
    if ( listen(list_s, LISTENQ) < 0 ) {
    	fprintf(stderr, "%s: Error calling listen()\n",progname);
    	exit(EXIT_FAILURE);
    }
	
   
    for(i=0;i<NUMTHRDS;i++){
     pthread_create(&ReadThd[i], NULL, LookupAndSend, (void *)list_s);
    }

    pthread_create(&UpdateThd, NULL, mytail, (void *)eventsfile);
    pthread_join(UpdateThd, (void **)&status);
    
   pthread_exit(NULL);
 
}

/*---functions---*/

/*  Read a line from a socket  */

ssize_t Readline(int sockd, void *vptr, size_t maxlen) {
    ssize_t n, rc;
    char    c, *buffer;

    buffer = vptr;

    for ( n = 1; n < maxlen; n++ ) {
	
	if ( (rc = read(sockd, &c, 1)) == 1 ) {
	    *buffer++ = c;
	    if ( c == '\n' )
		break;
	}
	else if ( rc == 0 ) {
	    if ( n == 1 )
		return 0;
	    else
		break;
	}
	else {
	    if ( errno == EINTR )
		continue;
	    return -1;
	}
    }

    *buffer = 0;
    return n;
}

/*  Write a line to a socket  */

ssize_t Writeline(int sockd, const void *vptr, size_t n) {
    size_t      nleft;
    ssize_t     nwritten;
    const char *buffer;
    

    buffer = vptr;
    nleft  = n;

     while ( nleft > 0 ) {
 	if ( (nwritten = write(sockd, (char *)vptr, nleft)) <= 0 ) {
	    if ( errno == EINTR ) {
		nwritten = 0;
	    }else{
		return -1;
	    }
	}
	nleft  -= nwritten;
	buffer += nwritten;
    }

    return n;
}

unsigned hash(char *s){

 unsigned hashval;

 for(hashval = 0; *s!='\0';s++){
  hashval = *s + 31 *hashval;
 }
 return hashval % HASHSIZE;
}


void *mytail (void *infile){    
        
    char **lines;
    int i, nlines = NLINES;
    
    nlines++;
    if((lines=malloc(nlines * sizeof *lines)) == 0){
        sysfatal("can't malloc lines: %r");
    }
    for(i=0; i < nlines; i++){
        if((lines[i]=malloc(MAX_CHARS)) == 0){
            sysfatal("can't malloc lines[%d]: %r", i);
	}
    }
 
   follow((char *)infile, lines, nlines);
   
    for(i=0; i < nlines; i++){
     free(lines[i]);
    }
    free(lines);

   return 0;
}

void
follow(char *infile, char *lines[], int n)
{
    FILE *fp;
    long off = 0;
    long old_off = 0;
    long real_off = 0;

    char   *tdir;
    time_t lnow;
    struct tm *timeptr;
    char   tnow[30];
    char   evfile[MAX_CHARS]="\0";
   
    if((tdir=calloc(STR_CHARS,1)) == 0){
     sysfatal("can't malloc tdir: %r");
    }
    strcat(tdir,spooldir);
    strcat(tdir,"/server_logs");

    for(;;){

/* In each cycle a new date file is costructed and is tested with the existing one
   when the date changes the new log file can be created later so we test if it is there
*/
     
        lnow=time(NULL);
        timeptr=localtime(&lnow);
        strftime(tnow,sizeof(tnow),"%Y%m%d",timeptr);

        evfile[0]=NUL;
        strcat(evfile,tdir);
        strcat(evfile,"/");
        strcat(evfile,tnow);

        if(strcmp(evfile,infile) != 0){

         infile = strdup(evfile);
         off = 0;

         while(1){
          if((fp=fopen((char *)infile, "r")) != 0){
           break;
          }
          sleep (1);
         }

        }else{

         if((fp=fopen((char *)infile, "r")) == 0){
          syserror("error opening %s: %r", infile);
          exit(EXIT_FAILURE);
         }

        }

        if(fseek(fp, off, SEEK_SET) < 0){
         sysfatal("couldn't seek: %r");
        }

        old_off=ftell(fp);
        fseek(fp, 0L, SEEK_END);
        real_off=ftell(fp);

        if(real_off < old_off){
         off=real_off;
        }else{
         off=old_off;
        }
   
        if(fseek(fp, off, SEEK_SET) < 0){
          sysfatal("couldn't seek: %r");
         }
        
        off = tail(fp, lines, n);
	fclose(fp);
	sleep(1);
    }        
}

long
tail(FILE *fp, char *lines[], int n)
{
    int i, j;
    long off=0;

    for(i=0; i < n; i++){
        *lines[i] = NUL;
    }
    i=0;

    while(fgets(lines[i], MAX_CHARS, fp)){
      if((strstr(lines[i],rex_queued)!=NULL) || (strstr(lines[i],rex_running)!=NULL) || (strstr(lines[i],rex_deleted)!=NULL) || (strstr(lines[i],rex_finished)!=NULL) || (strstr(lines[i],rex_hold)!=NULL)){
        if(++i == n){
            i = 0;
	}
      }else{
       *lines[i] = NUL;
      }
    }

    j = i;
    i = (i+1 == n ? 0 : i+1);
    while(i != j){
     if(*lines[i] != NUL){
       AddToStruct(lines[i]);
     }

     if(++i == n){
     	i = 0;
     }
    }
    if((off=ftell(fp)) < 0){
        sysfatal("couldn't get file location: %r");
    }
    return off;
}


int InfoAdd(int id, char *value, const char * flag){

 if((id <= 0) || (id >= HASHSIZE)){
  return -1;
 }
  
 /* set write lock */
  wlock=1;
 if((strcmp(flag,"JOBID")==0) && j2js[id] == NULL){
 
  j2js[id] = strdup("1");
  
 } else if(strcmp(flag,"JOBSTATUS")==0){
 
  j2js[id] = strdup(value);
 
 } else if(strcmp(flag,"EXITCODE")==0){

  j2ec[id] = strdup(value);
 
 } else {
 
 /* release write lock */
    wlock=0;
    
   return -1;
 
 }
   /* release write lock */
    wlock=0;
    
   return 0;
}

int AddToStruct(char *line){

 int n=0;
 int has_blah=0;
 unsigned h_blahjob;
 
 char *s_tok;
 char *j_tok;
 char *r_tok;
 char *b_tok;
 char *bb_tok;
 char *t_tok;

 char *	trex;
 char *	rex;
 
 int id;
 int is_queued=0;
 int is_finished=0;
 
 char *	tjobid=NULL;
 char *	jobid=NULL;

 char *	tex_status=NULL;
 char *	ex_status=NULL;

 char *tb_job=NULL;
 char *j_blahjob=NULL;
 
 char *blahjob_string="blahjob_";

 if(strstr(line,blahjob_string)!=NULL){
  has_blah=1;
 }
 
 s_tok=strtok(line,";");

 while(s_tok!=NULL){
  if(n==4){
   tjobid=strdup(s_tok);
  }else if(n==5){
   rex=strdup(s_tok);
   trex=strdup(rex);
  }
  s_tok=strtok(NULL,";");
  n++;
 }

/* get jobid */ 
 if(tjobid){
 
  n=0;
  j_tok=strtok(tjobid,".");
   while(j_tok!=NULL){
    if(n==0){
     jobid=strdup(j_tok);
     break;
    }
    j_tok=strtok(NULL,".");
    n++;
  }
 
  id=atoi(jobid);

 } /* close tjobid if */

/* get j_blahjob */
 if(rex && (strstr(rex,rex_queued)!=NULL)){
  is_queued=1; 
  n=0;
  b_tok=strtok(trex,",");
   while(b_tok!=NULL){
    if(n==2){
     tb_job=strdup(b_tok);
     break;
    }
    b_tok=strtok(NULL,",");
    n++;
  }
  
  n=0;
  bb_tok=strtok(tb_job,"=");
   while(bb_tok!=NULL){
    if(n==1){
     j_blahjob=strdup(bb_tok);
     break;
    }
    bb_tok=strtok(NULL,"=");
    n++;
  }
 
 } /* close rex_queued if */

/* get ex_status */
 if(rex && (strstr(rex,rex_finished)!=NULL)){
  is_finished=1;
  n=0;
  r_tok=strtok(trex,blank);
   while(r_tok!=NULL){
    if(n==0){
     tex_status=strdup(r_tok);
     break;
    }
    r_tok=strtok(NULL,blank);
    n++;
  }
 
  n=0;
  t_tok=strtok(tex_status,"=");
  while(t_tok!=NULL){
    if(n==1){
     ex_status=strdup(t_tok);
     break;
    }
    t_tok=strtok(NULL,"=");
    n++;
  }
  
 } /* close rex_finished if */
 
 while (1){
  if(rcounter==0){
   break;
  }
  sleep(1);
 } 
 
 if((is_queued==1) && (has_blah) && (j2js[id]==NULL)){

  InfoAdd(id,jobid,"JOBID");
 
  h_blahjob=hash(j_blahjob);
  bjl[h_blahjob]=strdup(jobid);
  
 } else if(j2js[id]!=NULL){
 
  if(rex && strstr(rex,rex_running)!=NULL){

   InfoAdd(id,"2","JOBSTATUS");
   
  } else if(strstr(rex,rex_deleted)!=NULL){
  
   InfoAdd(id,"3","JOBSTATUS");

  } else if(is_finished==1){
  
   InfoAdd(id,"4","JOBSTATUS");
   InfoAdd(id,ex_status,"EXITCODE");

  } else if(rex && strstr(rex,rex_hold)!=NULL){
  
   InfoAdd(id,"5","JOBSTATUS");

  } /* closes if-else if on rex_ */
 } /* closes if-else if on jobid lookup */
 
   free(rex);
   free(tjobid);
   free(jobid);
   free(tex_status);
   free(ex_status);
   free(tb_job);
   free(j_blahjob);

 return 0;
}

char *GetAllEvents(char *file){
 
 FILE *fp;
 char *line;
 char *opfile[STR_CHARS];
 int i=0;
 int maxtok;

 maxtok = strtoken(file, ' ', opfile);

 for(i=0; i<maxtok; i++){ 
  if((line=malloc(MAX_LINES)) == 0){
   sysfatal("can't malloc line: %r");
  }
 
  if((fp=fopen(opfile[i], "r")) != 0){
   while(fgets(line, MAX_LINES, fp)){
    if((strstr(line,rex_queued)!=NULL) || (strstr(line,rex_running)!=NULL) || (strstr(line,rex_deleted)!=NULL) || (strstr(line,rex_finished)!=NULL) || (strstr(line,rex_hold)!=NULL)){
     AddToStruct(line);
    }
   }
  } else {
   printf("Cannot open %s file\n",opfile[i]);
   exit(-1);
  }
  fclose(fp);
  free(line);


 } /* close for*/
    
 return NULL;

}

void *LookupAndSend(int m_sock){ 
    
    char      *buffer;
    char      *out_buf;
    char      *logdate;
    char      *jobid;
    char      h_jobid[20];
    char      *pr_removal="Not";
    int       i;
    int       id;
    int       conn_s;
    
    while ( 1 ) {


	/*  Wait for a connection, then accept() it  */
	
	if ( (conn_s = accept(m_sock, NULL, NULL) ) < 0 ) {
	    fprintf(stderr, "%s: Error calling accept()\n",progname);
	    exit(EXIT_FAILURE);
	}

	if((buffer=malloc(STR_CHARS)) == 0){
	  sysfatal("can't malloc buffer in LookupAndSend: %r");
	}
       buffer[0]='\0';

        /* read line from socket */
	Readline(conn_s, buffer, STR_CHARS-1);
	
	//printf("thread/0x%08lx\n",pthread_self());
	
	if((strlen(buffer)==0) || (strcmp(buffer,"\n")==0) || (strstr(buffer,"/")==0)){
         if((out_buf=malloc(STR_CHARS)) == 0){
          sysfatal("can't malloc out_buf in LookupAndSend: %r");
         }
     	 sprintf(out_buf,"Wrong string format/Not\n");
	 goto close;
	}
        logdate=strtok(buffer,"/");
        jobid=strtok(NULL,"/");
        strtok(jobid,"\n");
    
/* now jobid has also the machine part after the numeric part
but atoi does the same if the machine part is there or not and we need 
all the jobid in the output classad */

/* get jobid from blahjob id (needed by *_submit.sh) */
       
	if(strcmp(logdate,"BLAHJOB")==0){
         for(i=0;i<WRETRIES;i++){
	  if(wlock==0){
	   *h_jobid=NUL;
	   strcat(h_jobid," ");
	   strcat(h_jobid,jobid);
	   if(bjl[hash(h_jobid)]==NULL){
	    sleep(1);
	    continue;
	   }
           if((out_buf=malloc(STR_CHARS)) == 0){
            sysfatal("can't malloc out_buf in LookupAndSend: %r");
           }
     	   sprintf(out_buf,"%s\n",bjl[hash(h_jobid)]);
	   goto close;
	  }else{
	   sleep(1);
	  } 
	 }
	 if(i==WRETRIES){
          if((out_buf=malloc(STR_CHARS)) == 0){
           sysfatal("can't malloc out_buf in LookupAndSend: %r");
          }
	  sprintf(out_buf,"Blahjob id %s not found\n",h_jobid);
	  goto close;
	 }
	}
	
/* get all info from jobid */

        for(i=0;i<WRETRIES;i++){
	
	 if(wlock==0){
	 
 	  id=atoi(jobid);
	  
    	  if(j2js[id]!=NULL){
 
           if((out_buf=malloc(STR_CHARS)) == 0){
            sysfatal("can't malloc out_buf in LookupAndSend: %r");
           }
	   	   
           if((strcmp(j2js[id],"3")==0) || (strcmp(j2js[id],"4")==0)){
            pr_removal="Yes";
           } else {
            pr_removal="Not";
           }
           if(strcmp(j2js[id],"4")==0){
            sprintf(out_buf,"[BatchJobId=\"%s\"; JobStatus=%s; ExitCode=%s;/%s\n",jobid, j2js[id], j2ec[id], pr_removal);
           }else{
            sprintf(out_buf,"[BatchJobId=\"%s\"; JobStatus=%s;/%s\n",jobid, j2js[id], pr_removal);
           }
	   
	  } else {
	  
     	   GetEventsInOldLogs(logdate);
	   
     	   if(j2js[id]!=NULL){

            if((out_buf=malloc(STR_CHARS)) == 0){
             sysfatal("can't malloc out_buf in LookupAndSend: %r");
            }
	    
            if((strcmp(j2js[id],"3")==0) || (strcmp(j2js[id],"4")==0)){
             pr_removal="Yes";
            } else {
             pr_removal="Not";
            }
            if(strcmp(j2js[id],"4")==0){
             sprintf(out_buf,"[BatchJobId=\"%s\"; JobStatus=%s; ExitCode=%s;/%s\n",jobid, j2js[id], j2ec[id], pr_removal);
            }else{
             sprintf(out_buf,"[BatchJobId=\"%s\"; JobStatus=%s;/%s\n",jobid, j2js[id], pr_removal);
            }
	    
	   } else {
            if((out_buf=malloc(STR_CHARS)) == 0){
             sysfatal("can't malloc out_buf in LookupAndSend: %r");
            }
     	    sprintf(out_buf,"JobId %s not found/Not\n",jobid);
    	   }
	   
     	  }
     	  break;
	 } 
	 else {
	  sleep(1);
	 }
	   
        }
	
	if(i==WRETRIES){
         if((out_buf=malloc(STR_CHARS)) == 0){
          sysfatal("can't malloc out_buf in LookupAndSend: %r");
         }
	 sprintf(out_buf,"Cache locked/Not\n");
	}
close:	
 	Writeline(conn_s, out_buf, strlen(out_buf));
	
	free(out_buf);
	free(buffer);
	
	/*  Close the connected socket  */

	if ( close(conn_s) < 0 ) {
	    fprintf(stderr, "%s: Error calling close()\n",progname);
	    exit(EXIT_FAILURE);
	}
	
    } /* closes while */
    return(0); 
}

int GetEventsInOldLogs(char *logdate){

 char *loglist=NULL;
 
 loglist=GetLogList(logdate);
 if(loglist!=NULL){
  GetAllEvents(loglist);
 }
 
 return 0;
 
}

char *GetLogList(char *logdate){
 
 char datefile[STR_CHARS];
 char touch_out[STR_CHARS];
 char rm_out[STR_CHARS];
 char logs[MAX_CHARS]="\0";
 char *slogs;
 char *t_logs;
 char tlogs[MAX_CHARS];
 char command_string[MAX_CHARS]="\0";
 int n=0;
 FILE *mktemp_output;
 FILE *touch_output;
 FILE *find_output;
 FILE *rm_output;
 FILE *ls_output;
 int len;
 int last_tag;

 if((slogs=malloc(MAX_CHARS)) == 0){
  sysfatal("can't malloc slogs: %r");
 }
 
 sprintf(command_string,"mktemp -q /tmp/blahdate_XXXXXX");
 mktemp_output = popen(command_string,"r");
 if (mktemp_output != NULL){
  len = fread(datefile, sizeof(char), sizeof(datefile) - 1 , mktemp_output);
  if (len>0){
   datefile[len-1]='\000';
  }
 }
 pclose(mktemp_output);

/* We deal with both date format (20050513 and 200505130000.00) even if it is not needed */

 if(strlen(logdate) > 9){
  sprintf(command_string,"touch -t %s %s",logdate,datefile);
 } else {
  sprintf(command_string,"touch -d %s %s",logdate,datefile);
 }

 touch_output = popen(command_string,"r");
 if (touch_output != NULL){
  len = fread(touch_out, sizeof(char), sizeof(touch_out) - 1 , touch_output);
  if (len>0){
   touch_out[len-1]='\000';
  }
 }
 pclose(touch_output);
 
 if(LastLog!=NULL){
  sprintf(command_string,"find %s/* -type f -newer %s ! -newer %s -printf \"%%p \" 2>/dev/null", ldir, datefile, LastLog);
 } else{
  sprintf(command_string,"find %s/* -type f -newer %s -printf \"%%p \" 2>/dev/null", ldir, datefile);
 }
 
 find_output = popen(command_string,"r");
 if (find_output != NULL){
  len = fread(logs, sizeof(char), sizeof(logs) - 1 , find_output);
  if (len>0){
   logs[len-1]='\000';
  }
 }
 pclose(find_output);
  
 sprintf(command_string,"rm %s", datefile);
 rm_output = popen(command_string,"r");
 if (rm_output != NULL){
  len = fread(rm_out, sizeof(char), sizeof(rm_out) - 1 , rm_output);
  if (len>0){
   rm_out[len-1]='\000';
  }
 }
 pclose(rm_output);
 
/* this is done to avoid ls -tr to run without args so that local dir is listed */

 if((logs == NULL) || (strlen(logs) < 2)){
  return NULL;
 }
 
 sprintf(command_string,"ls -tr %s", logs);
 ls_output = popen(command_string,"r");
 if (ls_output != NULL){
  len = fread(tlogs, sizeof(char), sizeof(tlogs) - 1 , ls_output);
  if (len>0){
   tlogs[len-1]='\000';
  }
  pclose(ls_output);
 
  slogs[0]='\0';
  t_logs=strtok(tlogs,"\n");
  while(t_logs!=NULL){
   if(n==0){
    LastLog=strdup(t_logs);
    last_tag=n;
   }else if(n==1){
    last_tag=n; 
   }
   
   strcat(slogs,t_logs);
   strcat(slogs," ");

   t_logs=strtok(NULL,"\n");
   n++;
  }

/* last_tag is used to see if there is only one log file and to avoid to rescan it*/

  if(last_tag==0){
   return NULL;
  }
  
  return slogs;
  
 } else {
 
  return NULL;
  
 }
}

int strtoken(const char *s, char delim, char **token)
{
    char *tmp;
    char *ptr, *dptr;
    int i = 0;
    
    tmp = (char *) malloc(1 + strlen(s));
    assert(tmp);
    strcpy(tmp, s);
    ptr = tmp;
    while(1) {
        if((dptr = strchr(ptr, delim)) != NULL) {
            *dptr = '\0';
            token[i] = (char *) malloc(1 + strlen(ptr));
            assert(token[i]);
            strcpy(token[i], ptr);
            ptr = dptr + 1;
            i++;
        } else {
            if(strlen(ptr)) {
                token[i] = (char *) malloc(1 + strlen(ptr));
                assert(token[i]);
                strcpy(token[i], ptr);
                i++;
                break;
            } else{
	        break;
	    }
        }
    }
    
    token[i] = NULL;
    free(tmp);
    return i;
}

int ParseCmdLine(int argc, char *argv[], char **szPort, char **szSpoolDir) {
    
    int n = 1;
     
    if(argc==2 && !(!strncmp(argv[n], "-h", 2) || !strncmp(argv[n], "-H", 2))){
     *szPort= argv[n];
     return 0;
    }

    while ( n < argc ) {
        if ( !strncmp(argv[n], "-p", 2) || !strncmp(argv[n], "-P", 2) ) {
            *szPort= argv[++n];
        }
        else if ( !strncmp(argv[n], "-s", 2) || !strncmp(argv[n], "-S", 2) ) {
            *szSpoolDir = argv[++n];
        }
        else if ( !strncmp(argv[n], "-h", 2) || !strncmp(argv[n], "-H", 2) ) {
            printf("Usage:\n");
            printf("%s [-p] <remote_port [%d]> -s <PBS_spooldir [%s]>\n",progname, DEFAULT_PORT, spooldir);
	    
            exit(EXIT_SUCCESS);
        }
        ++n;
    }
    
    return 0;
}

/* the reset is error processing stuff */

void
eprint(int err, char *fmt, va_list args)
{
    extern int errno;

    fprintf(stderr, "%s: ", argv0);
    if(fmt)
        vfprintf(stderr, fmt, args);
    if(err)
        fprintf(stderr, "%s", strerror(errno));
    fputs("\n", stderr);
    errno = 0;
}

char *
chopfmt(char *fmt)
{
    static char errstr[ERRMAX];
    char *p;

    errstr[0] = NUL;
    if((p=strstr(fmt, "%r")) != 0)
        fmt = strncat(errstr, fmt, p-fmt);
    return fmt;
}

/* syserror: print error and continue */
void
syserror(char *fmt, ...)
{
    va_list args;
    char *xfmt;

    va_start(args, fmt);
    xfmt = chopfmt(fmt);
    eprint(xfmt!=fmt, xfmt, args);
    va_end(args);
}

/* sysfatal: print error and die */
void
sysfatal(char *fmt, ...)
{
    va_list args;
    char *xfmt;

    va_start(args, fmt);
    xfmt = chopfmt(fmt);
    eprint(xfmt!=fmt, xfmt, args);
    va_end(args);
    exit(1);
}
