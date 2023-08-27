mcli set account permission amax.did active --armoniaadmind-code

token_url='https://xdao.mypinata.cloud/ipfs/QmZab14Y7KbG12RqvUdtqeiSa1aJRqcDzU78cL1bTHDMxn'
tokend_id='1000001'
admin=armoniaadmin

# mpush did.ntoken create '["armoniaadmin",1000000000,['$token_id',0],"'$token_url'","armoniaadmin"]' -p armoniaadmin
# mpush did.ntoken issue '["armoniaadmin",[100000,['$token_id',0]],""]' -p armoniaadmin
# mcli set account permission amax.did active --add-code

#mpush did.ntoken issue '["armoniaadmin",[900000,[1000001,0]],""]' -p armoniaadmin
# mpush did.ntoken transfer '["armoniaadmin","amax.did",[[900000, [1000001, 0]]],"refuel"]' -p armoniaadmin

# mpush did.ntoken setacctperms '["armoniaadmin","amax.did",['$token_id',0],true,true]' -p armoniaadmin
# mpush did.ntoken transfer '["armoniaadmin","amax.did",[[100000, ['$token_id', 0]]],"refuel"]' -p armoniaadmin

## APLink Farm
land_uri='https://did.aplink.app'
banner_uri='https://xdao.mypinata.cloud/ipfs/QmQBYn2zCczpNXYc5RTwn1rTmrth7bLToopKNisCzKAQpU'

# mpush aplink.farm lease '["amax.did","DID reward","'$land_uri'","'$banner_uri'"]' -p armoniaadmin
lease_id=5
# mpush aplink.token transfer '["armoniaadmin","aplink.farm","10000000.0000 APL","'$lease_id'"]' -p armoniaadmin

# DID contract
# mpush amax.did init '["armoniaadmin","did.ntoken","amax.daodev", '$lease_id']' -p amax.did
# mpush amax.did addvendor '["alibaba","amax.daodev",1,"10.0000 APL","0.05000000 AMAX",['$token_id', 0]]' -p $admin


 mtbl did.ntoken amax.did accounts |grep amount