# System-wide csh startup (tcsh 6.24.16).
if (! $?PATH) then
    set path = (/bin /usr/bin /sbin /usr/sbin /usr/local/bin)
endif
if (! $?TERM) setenv TERM zhaoxu
if (! $?HOME) setenv HOME /root
if (! $?USER) setenv USER root
if ($?prompt) then
    set prompt = "%n@zhaoxu %~ %# "
    set autolist
    set noclobber
endif
