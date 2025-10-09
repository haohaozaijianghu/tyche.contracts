tyche_token=tyche.token
mreg flon $tyche_token flonian
mtran flonian $tyche_token "100 FLON"
mset $tyche_token flon.token
mcli set account permission $tyche_token active --add-code


mpush $tyche_token create '["flonian","10000000000.000000 TRUSD"]' -p $tyche_token
mpush $tyche_token issue '["flonian","100000000.000000 TRUSD","1st issue"]' -p flonian


mpush $tyche_token create '["flonian","10000000000.00000000 TYCHE"]' -p $tyche_token
mpush $tyche_token issue '["flonian","100000000.00000000 TYCHE","1st issue"]' -p flonian





