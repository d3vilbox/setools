#!/bin/sh

# command line shell to run seuser add/suseradd
#

    SEUSER=/usr/local/selinux/bin/seuser
    USERADD=/usr/local/selinux/bin/suseradd
    SEUSERDEL="/usr/local/selinux/bin/seuser delete"
    seuseropts=""
    suseraddopts=""
    comment=""
    ROLES=0

# short usage
usage() {
    echo >&2 "$@"
    echo >&2 "
usage: 
	$0  -X
	$0  -A
	$0  -h
	$0  -D [-g group] [-b base] [-s shell]
                     [-f inactive] [-e expire ]
	$0  [-u uid [-o]] [-g group] [-G group,...] 
                     [-d home] [-s shell] [-c comment] [-m [-k template]]
                     [-f inactive] [-e expire ] [-p passwd] [-M] [-n] [-r]
	             [-R role[,...]] username
"
}

# -h show this
long_usage() {
    usage ""
    echo >&2 "
    -X             start seuser gui (seuser -X)
    -A             activate policy (seuser load)
    -D             given alone, shows the system default settings for 
                   useradd as the first argument, sets defaults specified:
                    -g group    default group
                    -b base     default base
                    -s shell    default shell
                    -f days     inactive default number of days after 
                                password expires to disable account
                    -e date     default date accounts expire
    -u uid [-o]    specify userid, optional -o must be used if uid is not
		   unique
    -g group       initial group name or number
    -G group[,...] supplementary groups
    -d home_dir    user's home directory
    -c comment     password file comment field
    -e date        expiration date of account
    -f days        number of days after a password expires on which account 
                   disabled
    -m [-k dir]    create files in user's home dir, -k specifies directory 
                   other than default, /etc/skel, from which to copy files
    -M             do not create user's home directory
    -p password    initial password (encrypted)
    -s shell       user's login shell
    -n             turn off creation of group with same name as user 
                   (RedHat specific)
    -r             create a system account
    -R role[,...]  specify selinux roles for user
    -h             print out this usage message
"
# took -N out because we do that by default, and then a load as last action
#    -N             do not load policy (only build and install)
}

# if no arguments are given print usage statement
if [ $# -eq 0 ]; then
    usage ""
    exit 1
fi

# if first option is -D, then go straight to suseradd to change system defaults
while getopts ADu:og:G:d:s:c:mk:f:e:p:MnrXR:h optvar
do
    case "$optvar"
    in 
	D) # show or set user creation defaults
	    if [ ${OPTIND} -ne 2 ]; then
		usage "-D must be the first option"
	    fi
	    if [ $# -eq 1 ]; then # we want to see defaults
		${USERADD} -D
		exit $?
	    else # we are setting defaults
		${USERADD} "$*"
		# return exit code from suseradd
		exit $?
	    fi
	    exit 0
	    ;;
	A) # load policy
	    if [ $# -eq 1]; then # we're just loading the policy
		${SEUSER} load
		exit $?
	    fi
	    # -A is redundant otherwise so discard and continue
	    echo >&2 "Warning: -A ignored (must be used alone)"
	    ;;
        R) # roles to use
	    seuseropts="${seuseropts} -R ${OPTARG}"
	    ROLES=1
	    ;;
#	N) # option to NOT load policy
#	    seuseropts="${seuseropts} -N"
#	    ;;
	X) # start sueser gui
	    if [ ${OPTIND} -ne 2 ]; then
		usage "-X is for running seuser in gui mode"
		exit 1
	    fi
	    ${SEUSER} -g
	    exit $?
	    ;;
	u) # specify a uid, unique unless -o also used
	    suseraddopts="${suseraddopts} -u ${OPTARG}"
	    ;;
	o) # allow use of existing uid, must also use -u
	    suseraddopts="${suseraddopts} -o"
	    ;;	    
	g) # user's group name or id (must exist)
	    suseraddopts="${suseraddopts} -g ${OPTARG}"
	    ;; 
	G) # additional groups
	    suseraddopts="${suseraddopts} -G ${OPTARG}"
	    ;;
	d) # home directory
	    suseraddopts="${suseraddopts} -d ${OPTARG}"
	    ;;
	s) # login shell
	    suseraddopts="${suseraddopts} -s ${OPTARG}"
	    ;;
	c) # user's comment field (should be quoted)
	    comment="${OPTARG}"
	    ;;
	m) # create home dir from /etc/skel
	    suseraddopts="${suseraddopts} -m"
	    ;;	   
 	k) # specify location of skeleton_dir (for -m)
	    suseraddopts="${suseraddopts} -k ${OPTARG}"
	    ;;
	f) # disable account after password expires
	    suseraddopts="${suseraddopts} -f ${OPTARG}"
	    ;;
	e) # account expiration date
	    suseraddopts="${suseraddopts} -e ${OPTARG}"
	    ;;
	p) # encrypted initial password
	    suseraddopts="${suseraddopts} -p ${OPTARG}"
	    ;;
	M) # do not create home directory regardless of system settings
	    suseraddopts="${suseraddopts} -M"
	    ;;
	n) # do not create group with same name as user
	    suseraddopts="${suseraddopts} -n"
	    ;;
	r) # system account
	    suseraddopts="${suseraddopts} -r"
	    ;;
	h) # print usage
	    long_usage
	    exit 0
	    ;;	    
    esac
done
# toss out the arguments we've already processed
shift  `expr $OPTIND - 1`

# Here we expect the username
if [ $# -eq 0 ]; then
    usage "Need user name"
    exit 1
fi

USERNAME=$1

# see if user already exists in system
grep ${USERNAME} /etc/passwd > /dev/null
if [ $? -eq 0 ]; then
    echo >&2 "$USERNAME already exists"
    exit 1
fi

# system_u should be installed with the system by default.  If it's not
# there it must be added by hand (possible indication of bigger problems).
if [ "${USERNAME}" = "system_u" ]; then
    usage "system_u is a predefined user"
    exit 1
fi

# don't allow user_u to be added as a system user (intention implied by
# presence of useradd options)
if [ "${USERNAME}" = "user_u" ]; then 
    if [ -n "${suseraddopts}" -o -n "${comment}" ]; then
	echo >&2 "You cannot add the generic policy user, user_u, as a system user"
	exit 1
    fi
fi
if [ "${USERNAME}" = "user_u" -a $ROLES -eq 0 ]; then
     echo >&2 "You must provide roles for generic policy user, user_u"
     exit 1
fi

# look past the username
shift

# there should be nothing after the username
if [ $# -ne 0 ]; then
    usage "You're giving me some extra stuff: $@"
    exit 1
fi

# First add user to policy if roles were given
# We use 'change' instead of 'add' because of the case where
# the user is in the policy and not in the system.  In that
# case we want to overwrite the user definition in the policy.
# -f is because we haven't yet created the system user
# -N is because we don't load the policy until we're done
if [ $ROLES -eq 1 ]; then 
    echo >&2  "adding $USERNAME ${seuseropts} to policy"
    ${SEUSER} change -f -N ${seuseropts}  ${USERNAME} 2> /dev/null
    # seuser failed
    if [ $? -ne 0 ]; then
	RC=$?
	echo >&2 "Failed adding $USERNAME to policy; did not create system user"
	exit $RC
    fi
# if no roles are specified we are creating a generic user.  We 
# delete the user from the policy in case the user exists.  We 
# supress any error because most likely the user does not exist 
# in the policy.
else # no roles given -- generic user; ensure clean policy
    ${SEUSER} delete ${USERNAME} 2> /dev/null
fi

# add to system if not user_u
if [ "${USERNAME}" != "user_u" ]; then 
    # comments with spaces make life difficult so we need this
    if [ -n "${comment}" ]; then
	${USERADD} ${suseraddopts} -c "${comment}" ${USERNAME} 2> /dev/null
    else
	${USERADD} ${suseraddopts} ${USERNAME} 2> /dev/null
    fi
    RC=$?
    # if adding user to system failed, remove from policy if we added it
    if [ $RC -ne 0 ]; then
	echo >&2 "Failed adding $USERNAME to system."
	if [ $ROLES -eq 1 ]; then 
	    echo >&2 "Removing $USERNAME from policy."
	    ${SEUSERDEL} -N ${USERNAME} 2> /dev/null
	fi
	# exit with error code from suseradd
	exit $RC
    fi
fi

# if we were successful with everything we reach here and load the policy
${SEUSER} load 1>&2> /dev/null

