loan=tyche.l4
admin=ad
lp=joss
or=oracle3
user1=josstest
tnew $loan
tset $loan tyche.loan
tcli push action $loan init '["'$admin'", "'$lp'","'$or'", true]' -p $loan
tcli get table $loan $loan global

tcli push action $loan setcallatsym '[["6,METH", "amax.mtoken"], "eth"]' -p $admin
tcli get table $loan $loan collsyms

tcli push action amax.mtoken transfer '{"from": "'$user1'", "to": "'$loan'", "quantity": "1.00000000 METH", "memo": "deposit"}' -p joss


