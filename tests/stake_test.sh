stake=tyche.stk5
admin=tyche.admin
tnew $stake
tset $stake tyche.stake
tcli push action $stake init '["'$admin'","'$admin'",["6,MUSDT", "amax.mtoken"],["6,MUSDT", "amax.mtoken"]]' -p $stake
#1732982400  2024-12-01 00:00:00
#1764518400  2025-12-01 00:00:00
tcli push action $stake createlock '["test","100.000000 MUSDT",1732982400]' -p $stake
tcli push action $stake createlock '[ "test2", "100.000000 MUSDT",  1764518400 ]' -p $stake

tcli push action $stake increasets '[ "test",  1764518400 ]' -p $stake


tcli push action $stake balance '["test"]' -p $stake



tcli  get table $stake  $stake global
tcli  get table $stake  $stake earnslocked
tcli  get table $stake  test  olduserpoint

tcli  get table $stake  $stake pointhistory


#ACTION init( const name& lp_refueler, const extended_symbol& principal_token, const extended_symbol& lp_token);





