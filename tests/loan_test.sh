loan=tyche.l2
admin=ad
lp=joss
or=oracle3
tnew $loan
tset $loan tyche.loan
tcli push action $loan init '["'$admin'", "'$lp'","'$or'", true]' -p $loan
tcli get table $loan $loan global

tcli push action $loan setcallatsym '[["6,METH", "amax.mtoken"], "eth"]' -p $loan