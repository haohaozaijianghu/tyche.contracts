loan=tyche.l1
admin=tyche.admin
tnew $loan
tset $loan tyche.loan
tcli push action $loan addrewardsym '{"sym":{ "sym":"6,TRUSD", "contract":"tyche.token" }}' -p $earn
