#!/bin/sh
echo "#define BUILD_VERSION \"$(git describe --tags 2> /dev/null|| echo unknown)\""
echo "#define COMMIT_DATE $(git log -1 --pretty=format:%ct%n || date -u +%s)"
echo "#define NCOMMIT $(git log --pretty=oneline | wc -l)"
date -u "+#define BUILD_DATE %s"
