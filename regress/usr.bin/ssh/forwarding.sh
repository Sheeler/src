#	$OpenBSD: forwarding.sh,v 1.16 2016/04/14 23:57:17 djm Exp $
#	Placed in the Public Domain.

tid="local and remote forwarding"

start_sshd

base=33
last=$PORT
fwd=""
CTL=$OBJ/ctl-sock
rm -f $CTL

for j in 0 1 2; do
	for i in 0 1 2; do
		a=$base$j$i
		b=`expr $a + 50`
		c=$last
		# fwd chain: $a -> $b -> $c
		fwd="$fwd -L$a:127.0.0.1:$b -R$b:127.0.0.1:$c"
		last=$a
	done
done
for p in ${SSH_PROTOCOLS}; do
	q=`expr 3 - $p`
	if ! ssh_version $q; then
		q=$p
	fi
	trace "start forwarding, fork to background"
	${SSH} -$p -F $OBJ/ssh_config -f $fwd somehost sleep 10

	trace "transfer over forwarded channels and check result"
	${SSH} -$q -F $OBJ/ssh_config -p$last -o 'ConnectionAttempts=4' \
		somehost cat ${DATA} > ${COPY}
	test -s ${COPY}		|| fail "failed copy of ${DATA}"
	cmp ${DATA} ${COPY}	|| fail "corrupted copy of ${DATA}"

	sleep 10
done

for p in ${SSH_PROTOCOLS}; do
for d in L R; do
	trace "exit on -$d forward failure, proto $p"

	# this one should succeed
	${SSH} -$p -F $OBJ/ssh_config \
	    -$d ${base}01:127.0.0.1:$PORT \
	    -$d ${base}02:127.0.0.1:$PORT \
	    -$d ${base}03:127.0.0.1:$PORT \
	    -$d ${base}04:127.0.0.1:$PORT \
	    -oExitOnForwardFailure=yes somehost true
	if [ $? != 0 ]; then
		fail "connection failed, should not"
	else
		# this one should fail
		${SSH} -q -$p -F $OBJ/ssh_config \
		    -$d ${base}01:127.0.0.1:$PORT \
		    -$d ${base}02:127.0.0.1:$PORT \
		    -$d ${base}03:127.0.0.1:$PORT \
		    -$d ${base}01:localhost:$PORT \
		    -$d ${base}04:127.0.0.1:$PORT \
		    -oExitOnForwardFailure=yes somehost true
		r=$?
		if [ $r != 255 ]; then
			fail "connection not termintated, but should ($r)"
		fi
	fi
done
done

for p in ${SSH_PROTOCOLS}; do
	trace "simple clear forwarding proto $p"
	${SSH} -$p -F $OBJ/ssh_config -oClearAllForwardings=yes somehost true

	trace "clear local forward proto $p"
	${SSH} -$p -f -F $OBJ/ssh_config -L ${base}01:127.0.0.1:$PORT \
	    -oClearAllForwardings=yes somehost sleep 10
	if [ $? != 0 ]; then
		fail "connection failed with cleared local forwarding"
	else
		# this one should fail
		${SSH} -$p -F $OBJ/ssh_config -p ${base}01 true \
		     >>$TEST_REGRESS_LOGFILE 2>&1 && \
			fail "local forwarding not cleared"
	fi
	sleep 10
	
	trace "clear remote forward proto $p"
	${SSH} -$p -f -F $OBJ/ssh_config -R ${base}01:127.0.0.1:$PORT \
	    -oClearAllForwardings=yes somehost sleep 10
	if [ $? != 0 ]; then
		fail "connection failed with cleared remote forwarding"
	else
		# this one should fail
		${SSH} -$p -F $OBJ/ssh_config -p ${base}01 true \
		     >>$TEST_REGRESS_LOGFILE 2>&1 && \
			fail "remote forwarding not cleared"
	fi
	sleep 10
done

for p in 2; do
	trace "stdio forwarding proto $p"
	cmd="${SSH} -$p -F $OBJ/ssh_config"
	$cmd -o "ProxyCommand $cmd -q -W localhost:$PORT somehost" \
		somehost true
	if [ $? != 0 ]; then
		fail "stdio forwarding proto $p"
	fi
done

echo "LocalForward ${base}01 127.0.0.1:$PORT" >> $OBJ/ssh_config
echo "RemoteForward ${base}02 127.0.0.1:${base}01" >> $OBJ/ssh_config
for p in ${SSH_PROTOCOLS}; do
	trace "config file: start forwarding, fork to background"
	${SSH} -S $CTL -M -$p -F $OBJ/ssh_config -f somehost sleep 10

	trace "config file: transfer over forwarded channels and check result"
	${SSH} -F $OBJ/ssh_config -p${base}02 -o 'ConnectionAttempts=4' \
		somehost cat ${DATA} > ${COPY}
	test -s ${COPY}		|| fail "failed copy of ${DATA}"
	cmp ${DATA} ${COPY}	|| fail "corrupted copy of ${DATA}"

	${SSH} -S $CTL -O exit somehost
done

for p in 2; do
	trace "transfer over chained unix domain socket forwards and check result"
	rm -f $OBJ/unix-[123].fwd
	${SSH} -f -F $OBJ/ssh_config -R${base}01:[$OBJ/unix-1.fwd] somehost sleep 10
	${SSH} -f -F $OBJ/ssh_config -L[$OBJ/unix-1.fwd]:[$OBJ/unix-2.fwd] somehost sleep 10
	${SSH} -f -F $OBJ/ssh_config -R[$OBJ/unix-2.fwd]:[$OBJ/unix-3.fwd] somehost sleep 10
	${SSH} -f -F $OBJ/ssh_config -L[$OBJ/unix-3.fwd]:127.0.0.1:$PORT somehost sleep 10
	${SSH} -F $OBJ/ssh_config -p${base}01 -o 'ConnectionAttempts=4' \
		somehost cat ${DATA} > ${COPY}
	test -s ${COPY}			|| fail "failed copy ${DATA}"
	cmp ${DATA} ${COPY}		|| fail "corrupted copy of ${DATA}"

	#wait
	sleep 10
done
