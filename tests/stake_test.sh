stake=tyche.stk22
admin=tyche.admin
tnew $stake
tset $stake tyche.stake
tcli push action $stake init '["'$admin'","'$admin'",["6,MUSDT", "amax.mtoken"],["6,MUSDT", "amax.mtoken"]]' -p $stake
#1732982400  2024-12-01 00:00:00
#1764518400  2025-12-01 00:00:00

1701513909
tcli push action $stake createlock '["test","100.000000 MUSDT",1701750184]' -p $stake
tcli push action $stake createlock '[ "test2", "100.000000 MUSDT",  1764518400 ]' -p $stake

tcli push action $stake inctime '[ "test",  1764518400 ]' -p $stake
tcli push action $stake incamount '[ "test",  "100.000000 MUSDT" ]' -p $stake
tcli push action $stake withdraw '[ "test" ]' -p test


tcli push action $stake balance '["test2"]' -p $stake
tcli push action $stake balance '["test"]' -p $stake
tcli push action $stake totalsupply '[]' -p $stake
tcli push action $stake totalsupply2 '[1701506586]' -p $stake




tcli  get table $stake  $stake global
tcli  get table $stake  $stake earnslocked
tcli  get table $stake  test  olduserpoint

tcli  get table $stake  $stake pointhistory


#ACTION init( const name& lp_refueler, const extended_symbol& principal_token, const extended_symbol& lp_token);





