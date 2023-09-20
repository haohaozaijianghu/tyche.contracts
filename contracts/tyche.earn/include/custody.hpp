using namespace std;
using namespace eosio;
#define CUSTODY_TBL [[eosio::table, eosio::contract("ido.custody")]]

class [[eosio::contract("ido.custody")]] custody: public eosio::contract {

public:
    using contract::contract;

    custody(eosio::name receiver, eosio::name code, eosio::datastream<const char*> ds):contract(receiver, code, ds){}

    ACTION addissue(const eosio::name& receiver, const uint64_t& ido_id, const eosio::asset& quantity);

    using add_issue_action = eosio::action_wrapper<"addissue"_n, &custody::addissue>;
            
}; //contract custody
