#!/bin/bash -l
#
# File:     lsf_submit.sh
# Author:   David Rebatto (david.rebatto@mi.infn.it)
#
# Revision history:
#     8-Apr-2004: Original release
#    28-Apr-2004: Patched to handle arguments with spaces within (F. Prelz)
#                 -d debug option added (print the wrapper to stderr without submitting)
#    10-May-2004: Patched to handle environment with spaces, commas and equals
#    13-May-2004: Added cleanup of temporary file when successfully submitted
#    18-May-2004: Search job by name in log file (instead of searching by jobid)
#     8-Jul-2004: Try a chmod u+x on the file shipped as executable
#                 -w option added (cd into submission directory)
#    21-Sep-2004: -q option added (queue selection)
#    29-Sep-2004: -g option added (gianduiotto selection) and job_ID=job_ID_log
#    13-Jan-2005: -n option added (MPI job selection) and changed prelz@mi.infn.it with
#                    blahp_sink@mi.infn.it
#     4-Mar-2005: Dgas(gianduia) removed. Proxy renewal stuff added (-r -p -l flags)
#     3-May-2005: Added support for Blah Log Parser daemon (using the lsf_BLParser flag)
#    31-May-2005: Separated job's standard streams from wrapper's ones
#    26-Jul-2007: Restructured to use common shell functions.
# 
#
# Description:
#   Submission script for LSF, to be invoked by blahpd server.
#   Usage:
#     lsf_submit.sh -c <command> [-i <stdin>] [-o <stdout>] [-e <stderr>] [-w working dir] [-- command's arguments]
#
#
#  Copyright (c) 2004 Istituto Nazionale di Fisica Nucleare (INFN).
#  All rights reserved.
#  See http://grid.infn.it/grid/license.html for license details.
#
#

. `dirname $0`/blah_common_submit_functions.sh

conffile=$lsf_confpath/lsf.conf

lsf_base_path=`cat $conffile|grep LSB_SHAREDIR| awk -F"=" '{ print $2 }'`

lsf_clustername=`${lsf_binpath}/lsid | grep 'My cluster name is'|awk -F" " '{ print $5 }'`
logpath=$lsf_base_path/$lsf_clustername/logdir

logfilename=lsb.events

bls_job_id_for_renewal=LSB_JOBID

srvfound=""

original_args="$@"
bls_parse_submit_options "$@"

if [ "x$lsf_nologaccess" != "xyes" -a "x$lsf_nochecksubmission" != "xyes" ]; then

#Try different log parser
 if [ ! -z $lsf_num_BLParser ] ; then
  for i in `seq 1 $lsf_num_BLParser` ; do
   s=`echo lsf_BLPserver${i}`
   p=`echo lsf_BLPport${i}`
   eval tsrv=\$$s
   eval tport=\$$p
   testres=`echo "TEST/"|$bls_BLClient -a $tsrv -p $tport`
   if [ "x$testres" == "xYLSF" ] ; then
    lsf_BLPserver=$tsrv
    lsf_BLPport=$tport
    srvfound=1
    break
   fi
  done
  if [ -z $srvfound ] ; then
   echo "1ERROR: not able to talk with no logparser listed"
   exit 0
  fi
 fi
fi

bls_setup_all_files

# Write wrapper preamble
cat > $bls_tmp_file << end_of_preamble
#!/bin/bash
# LSF job wrapper generated by `basename $0`
# on `/bin/date`
#
# LSF directives:
#BSUB -L /bin/bash
#BSUB -J $bls_tmp_name
end_of_preamble

#set the queue name first, so that the local script is allowed to change it
#(as per request by CERN LSF admins).
# handle queue overriding
[ -z "$bls_opt_queue" ] || grep -q "^#BSUB -q" $bls_tmp_file || echo "#BSUB -q $bls_opt_queue" >> $bls_tmp_file

[ -z "$bls_opt_mpinodes" ]       || echo "#BSUB -n $bls_opt_mpinodes" >> $bls_tmp_file

#local batch system-specific file output must be added to the submit file
if [ ! -z $bls_opt_req_file ] ; then
    echo \#\!/bin/sh >> ${bls_opt_req_file}-temp_req_script 
    cat $bls_opt_req_file >> ${bls_opt_req_file}-temp_req_script
    echo "source ${GLITE_LOCATION:-/opt/glite}/bin/lsf_local_submit_attributes.sh" >> ${bls_opt_req_file}-temp_req_script 
    chmod +x ${bls_opt_req_file}-temp_req_script 
    ${bls_opt_req_file}-temp_req_script  >> $bls_tmp_file 2> /dev/null
    rm -f ${bls_opt_req_file}-temp_req_script 
fi

if [ ! -z "$bls_opt_xtra_args" ] ; then
    echo -e $bls_opt_xtra_args >> $bls_tmp_file 2> /dev/null
fi

# Write LSF directives according to command line options

# File transfer directives. Input and output sandbox
bls_fl_subst_and_dump inputsand "#BSUB -f \"@@F_LOCAL > @@F_REMOTE\"" $bls_tmp_file
bls_fl_subst_and_dump outputsand "#BSUB -f \"@@F_LOCAL < @@F_REMOTE\"" $bls_tmp_file

# Accommodate for CERN-specific job subdirectory creation.
echo "" >> $bls_tmp_file
echo "# Check whether we need to move to the LSF original CWD:" >> $bls_tmp_file
echo "if [ -d \"\$CERN_STARTER_ORIGINAL_CWD\" ]; then" >> $bls_tmp_file
echo "    cd \$CERN_STARTER_ORIGINAL_CWD" >> $bls_tmp_file
echo "fi" >> $bls_tmp_file

bls_add_job_wrapper

# Let the wrap script be at least 1 second older than logfile
# for subsequent "find -newer" command to work
sleep 1


###############################################################
# Submit the script
###############################################################

datenow=`date +%Y%m%d`
bsub_out=`cd && ${lsf_binpath}/bsub -o /dev/null -e /dev/null -i /dev/null < $bls_tmp_file`
retcode=$?
if [ "$retcode" != "0" ] ; then
        rm -f $bls_tmp_file
        exit 1
fi

jobID=`echo "$bsub_out" | awk -F" " '{ print $2 }' | sed "s/>//" |sed "s/<//"`

if [ "x$jobID" == "x" ] ; then
        rm -f $bls_tmp_file
        exit 1
fi

if [ "x$lsf_nologaccess" != "xyes" -a "x$lsf_nochecksubmission" != "xyes" ]; then

# Don't trust bsub retcode, it could have crashed
# between submission and id output, and we would
# loose track of the job

# Search for the job in the logfile using job name

# Sleep for a while to allow job enter the queue
sleep 5

# find the correct logfile (it must have been modified
# *more* recently than the wrapper script)

logfile=""
jobID_log=""
log_check_retry_count=0

while [ "x$logfile" == "x" -a "x$jobID_log" == "x" ]; do

 cliretcode=0
 if [ "x$lsf_BLParser" == "xyes" ] ; then
     jobID_log=`echo BLAHJOB/$bls_tmp_name| $bls_BLClient -a $lsf_BLPserver -p $lsf_BLPport`
     cliretcode=$?
 fi
 
 if [ "$cliretcode" == "1" -a "x$lsf_fallback" == "xno" ] ; then
   ${lsf_binpath}/bkill $jobID
   echo "Error: not able to talk with logparser on ${lsf_BLPserver}:${lsf_BLPport}" >&2
   echo Error # for the sake of waiting fgets in blahpd
   rm -f $bls_tmp_file
   exit 1
 fi

 if [ "$cliretcode" == "1" -o "x$lsf_BLParser" != "xyes" ] ; then

   logfile=`find $logpath -name "$logfilename*" -type f -newer $bls_tmp_file -exec grep -lP "\"JOB_NEW\" \"[0-9\.]+\" [0-9]+ $jobID " {} \;`

   if [ "x$logfile" != "x" ] ; then

     jobID_log=`grep \"JOB_NEW\" $logfile | awk -F" " '{ print $4" " $42 }' | grep $bls_tmp_file|awk -F" " '{ print $1 }'`
   fi
 fi
 
 if (( log_check_retry_count++ >= 12 )); then
     ${lsf_binpath}/bkill $jobID
     echo "Error: job not found in logs" >&2
     echo Error # for the sake of waiting fgets in blahpd
     rm -f $bls_tmp_file
     exit 1
 fi

 let "bsleep = 2**log_check_retry_count"
 sleep $bsleep

done

jobID_check=`echo $jobID_log|egrep -e "^[0-9]+$"`

if [ "$jobID_log" != "$jobID" -a "x$jobID_log" != "x" -a "x$jobID_check" != "x" ]; then
    echo "WARNING: JobID in log file is different from the one returned by bsub!" >&2
    echo "($jobID_log != $jobID)" >&2
    echo "I'll be using the one in the log ($jobID_log)..." >&2
    jobID=$jobID_log
fi

fi #end if on $lsf_nologaccess

# Compose the blahp jobID (date + lsf jobid)
blahp_jobID="lsf/${datenow}/$jobID"


if [ "x$job_registry" != "x" ]; then
  now=`date +%s`
  let now=$now-1
  `dirname $0`/blah_job_registry_add "$blahp_jobID" "$jobID" 1 $now "$bls_opt_creamjobid" "$bls_proxy_local_file" 1
fi

echo ""
echo "BLAHP_JOBID_PREFIX$blahp_jobID"

bls_wrap_up_submit

exit $retcode
