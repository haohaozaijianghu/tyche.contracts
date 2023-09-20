#include "aplink.farmdb.hpp"

using namespace std;
using namespace wasm::db;

namespace aplink {

// #define CHECKC(exp, code, msg) \
//    { if (!(exp)) eosio::check(false, string("$$$") + to_string((int)code) + string("$$$ ") + msg); }
   
enum class err: uint8_t {
   NONE                 = 0,
   TIME_INVALID         = 1,
   RECORD_EXISTING      = 2,
   RECORD_NOT_FOUND     = 3,
   SYMBOL_MISMATCH      = 4,
   PARAM_ERROR          = 5,
   PAUSED               = 6,
   NO_AUTH              = 7,
   NOT_POSITIVE         = 8,
   NOT_STARTED          = 9,
   OVERSIZED            = 10,
   TIME_EXPIRED         = 11,
   NOTIFY_UNRELATED     = 12,
   ACTION_REDUNDANT     = 13,
   ACCOUNT_INVALID      = 14,
   CONTENT_LENGTH_INVALID = 15,
   NOT_DISABLED          = 16,

};

using std::string;
using std::vector;
using namespace eosio;

class [[eosio::contract("aplink.farm")]] farm: public eosio::contract {
private:
    global_singleton    _global;
    global_t            _gstate;
    dbc                 _db;

public:
    using contract::contract;

    /**
     * @brief farmer can plant apples to customer
     * 
     * @param lease_id 
     * @param farmer  send apples to account
     * @param quantity
     * @param memo 
     */
    ACTION allot(const uint64_t& lease_id, const name& farmer, const asset& quantity, const string& memo);
    using allot_action = eosio::action_wrapper<"allot"_n, &farm::allot>;

    /**
     * @brief 
     * 
     * @param apl_farm_contract 
     * @param lease_id 
     * @param apples - available apples
     */
    static void available_apples( const name& apl_farm_contract, const uint64_t& lease_id, asset& apples )
    {
        auto db         = dbc( apl_farm_contract );
        auto lease      = lease_t(lease_id);
        auto now        = time_point_sec(current_time_point());

        if (!db.get(lease) ||
            now < lease.opened_at || 
            now > lease.closed_at ||
            lease.status != lease_status::active) 
            apples = asset(0, APLINK_SYMBOL);
        
        apples = lease.available_apples;
    }
};

}