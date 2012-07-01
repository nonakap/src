#!/bin/sh

#
# user setting
#
dest_basedir=/home/snapshot/NetBSD
release_basedir=/home/snapshot/NetBSD
tools_basedir=/home/snapshot/NetBSD/netbsd-tools
build_sh_dir=.
log_dir=.
parallel=3
sudo_cmd=/usr/pkg/bin/sudo

#
# script start
#
LANG=C; export LANG
LC_ALL=C; export LC_ALL
LC_CTYPE=C; export LC_CTYPE

progname=$0

usage() {
  echo "Usage: ${progname} <machine> <date> <operation> [operation] [options]"
  exit 1
}

if [ $# -lt 3 ]; then
  break
fi

if [ "x$1" = "x" ]; then
  usage
fi
target_machine=$1
shift

if [ "x$1" = "x" ]; then
  usage
fi
build_date=$1
shift

if [ "x$1" = "x" ]; then
  usage
fi
first_operation=
operations=
while true
do
  if [ $# -eq 0 ]; then
    break
  fi

  operation=$1
  echo "${operation}" | grep "^-" > /dev/null 2>&1
  if [ $? -eq 0 ]; then
    break
  fi
  shift

  real_operation=`echo "${operation}" | sed -e 's/\([^=]*\)[=].*/\1/'`
  case "${real_operation}" in
  install|installmodules)
    if [ `id -u` -ne 0 ]; then
      if [ -x ${sudo_cmd} ]; then
        runcmd=${sudo_cmd}
      else
        echo "Please run as root user."
        exit 1
      fi
    fi
    ;;
  *)
    ;;
  esac
  operations="${operations} ${operation}"

  if [ "x${first_operation}" = "x" ]; then
    first_operation="${real_operation}"
  fi
done
if [ "x${operations}" = "x" ]; then
  usage
fi
build_opt="${build_opt} -U"

while true
do
  if [ $# -eq 0 ]; then
    break
  fi

  opt=$1
  shift
  echo "${opt}" | grep " " > /dev/null 2>&1
  if [ $? -eq 0 ]; then
    opt="\"${opt}\""
  fi
  build_opt="${build_opt} ${opt}"
done
if [ "${parallel}" -gt 1 ]; then
  build_opt="${build_opt} -j ${parallel}"
fi

if [ "x${tools_basedir}" != "x" ]; then
  build_opt="-T ${tools_basedir}/${target_machine} ${build_opt}"
fi

${runcmd} time ${build_sh_dir}/build.sh -m ${target_machine} \
  -D ${dest_basedir}/${build_date}/root/${target_machine} \
  -R ${release_basedir}/${build_date}/release \
  ${build_opt} \
  ${operations} \
  2>&1 | tee ${log_dir}/${build_date}-${target_machine}-${first_operation}.log
retval=$?
exit ${retval}
