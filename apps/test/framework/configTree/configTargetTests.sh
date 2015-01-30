#!/bin/bash

#---------------------------------------------------------------------------------------------------
#  Bash script for running the configTree ACL tests on target.
#
#  Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
#  Use of this work is subject to license.
#---------------------------------------------------------------------------------------------------

targetAddr=$1
targetType=${2:-ar7}
appList="cfgSelfRead cfgSelfWrite cfgSystemRead cfgSystemWrite"




#---------------------------------------------------------------------------------------------------
#  Checks if the logStr is in the logs.
#
#  params:
#      comparison  Either "==", ">", "<", "!="
#      numMatches  The number of expected matches.
#      logStr      The log string to search for.
#---------------------------------------------------------------------------------------------------
function checkLogStr
{
    numMatches=$(ssh root@$targetAddr "/sbin/logread | grep -c \"$3\"")

    echo "$numMatches matches for '$3' "

    if [ ! $numMatches $1 $2 ]
    then
        echo "$numMatches occurances of '$3' but $1 $2 expected"
        echo "ConfigTree Test Failed!"
        exit 1
    fi
}




#---------------------------------------------------------------------------------------------------
#  Checks the return value of the last executed command.  If the return value indicates failure,
#  then it will exit the script.
#---------------------------------------------------------------------------------------------------
function CheckRet
{
    RETVAL=$?

    if [ $RETVAL -ne 0 ]; then
        echo -e $COLOR_ERROR "Exit Code $RETVAL" $COLOR_RESET
        exit $RETVAL
    fi
}




#---------------------------------------------------------------------------------------------------
#  Run our test apps on the device.
#---------------------------------------------------------------------------------------------------
function runApps
{
    echo "##########  Stopping all other apps on device '$targetAddr'."
    ssh root@$targetAddr "/usr/local/bin/app stop \"*\""
    sleep 1

    echo "##########  Clear the logs."
    ssh root@$targetAddr "killall syslogd"
    CheckRet
    # Must restart syslog this way so that it gets the proper SMACK label.
    ssh root@$targetAddr "/mnt/flash/startup/fg_02_RestartSyslogd"
    CheckRet

    echo "##########  Run all the apps."
    for app in $appList
    do
        ssh root@$targetAddr "/usr/local/bin/app start $app"
        CheckRet
    done
}




#---------------------------------------------------------------------------------------------------
#  Exec the config tool on the device to set a string in a config tree.
#---------------------------------------------------------------------------------------------------
function rconfigset
{
    ssh root@$targetAddr "/usr/local/bin/config set $1 $2"
}




#---------------------------------------------------------------------------------------------------




echo "*****************  configTree ACL Tests started.  *****************"

echo "##########  Make sure Legato is running on device '$targetAddr'."
ssh root@$targetAddr "/usr/local/bin/legato start"
CheckRet


echo "##########  Install all the apps to device '$targetAddr'."
appDir="$LEGATO_ROOT/build/$targetType/bin/tests"
cd "$appDir"
CheckRet
for app in $appList
do
    echo "  Installing '$appDir/$app.$targetType'"
    instapp $app.$targetType $targetAddr
    CheckRet
done


echo "##########  Listing apps on device '$targetAddr'."
lsapp $targetAddr


runApps


echo "##########  Checking log results."
checkLogStr "==" 1 "=====  Read ACL Test on tree: cfgSelfRead, successful.  ====="
checkLogStr "==" 0 "=====  Write ACL Test on tree: cfgSelfWrite, successful.  ====="
checkLogStr "==" 0 "=====  Read ACL Test on tree: system, successful.  ====="
checkLogStr "==" 0 "=====  Write ACL Test on tree: system, successful.  ====="



echo "##########  Updating app permissions."
rconfigset "/apps/cfgSelfWrite/configLimits/acl/cfgSelfWrite" "write"
rconfigset "/apps/cfgSystemRead/configLimits/allAccess" "read"
rconfigset "/apps/cfgSystemWrite/configLimits/allAccess" "write"



runApps


echo "##########  Checking log results."
checkLogStr "==" 1 "=====  Read ACL Test on tree: cfgSelfRead, successful.  ====="
checkLogStr "==" 1 "=====  Write ACL Test on tree: cfgSelfWrite, successful.  ====="
checkLogStr "==" 1 "=====  Read ACL Test on tree: system, successful.  ====="
checkLogStr "==" 1 "=====  Write ACL Test on tree: system, successful.  ====="




echo "##########  Removing the apps."
for app in $appList
do
    rmapp $app $targetAddr
    CheckRet
done
