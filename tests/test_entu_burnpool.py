import time
import unittest
from amaxfactory.eosf import *
from amaxfactory.core.logger import Verbosity

verbosity([Verbosity.INFO, Verbosity.OUT])

CONTRACT_WASM_PATH = "/Users/joslin/code/workspace/amaxfactory/templates/wasm/"

CUSTOMER_WASM_PATH = "/Users/joslin/code/contracts/entu.contracts"

MASTER = MasterAccount()
HOST = Account()

class Test(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        SCENARIO('''
        Create a contract from template, then build and deploy it.
        ''')
        reset("/tmp/dex_spot.log")
    
    
    @classmethod
    def tearDownClass(cls):
        # time.sleep(1000)

        stop()
        
    def test_register(self):
        '''The only test function.

        The account objects `master, host, alice, ...` which are of the global namespace, do not have to be explicitly declared (and still keep the linter silent).
        '''
        
        dex_path = CUSTOMER_WASM_PATH
        init.build(dex_path)        
        burnpool = init.DEX_BURNPOOL()
        
        COMMENT('''
        finished
        ''')
        master = new_master_account()
    
        oooo = new_account(master, "oooo")
        
        amax_token  = init.AMAX_TOKEN().setup()
        amax_mtoken = init.AMAX_MTOKEN().setup()
        
        admin = new_account(master, "admin")
        amax_token.push_action(
        "create",
        {
            "issuer": admin,
            "maximum_supply": "1000000000.0000 ENTU"
        },
        permission=[(admin, Permission.ACTIVE), (amax_token, Permission.ACTIVE)])

        amax_token.push_action(
        "issue",
        {
            "to": admin, "quantity": "800000000.0000 ENTU", "memo": ""
        },
        permission=(admin, Permission.ACTIVE))
        
        
        burnpool.setsympair('6,MUSDT', "amax.mtoken", '100000.0000 ENTU', burnpool)
        
    
        p1 = self.init_seller_account(master, admin )
        s1 = self.init_buyer_account(master, admin)
        s1.getBalance("AMAX")
        
        COMMENT('''
        finished
        ''')
        
        
        time.sleep(1)
        p1.transfer(burnpool, '6.000000 MUSDT', "dddddd")
        p1.transfer(burnpool, '8.000000 MUSDT', "dddddd")
        p1.transfer(burnpool, '10.000000 MUSDT', "dddddd")
        p1.transfer(burnpool, '12.000000 MUSDT', "dddddd")
        p1.transfer(burnpool, '14.000000 MUSDT', "dddddd")
        s1.transfer(burnpool, '1000.00000000 FFT', "6,MUSDT")
        s1.transfer(burnpool, '2000.00000000 FFT', "6,MUSDT")
        
        time.sleep(10000)
        
        
    def init_buyer_account(self, p1, admin):
        b1 = new_account(p1, factory=True)
        admin.transfer(b1, "20000.00000000 FFT", "")
        return b1
    
    def init_seller_account(self, p1, admin):
        s1 = new_account(p1, factory=True)
        admin.transfer(s1, "20000.00000000 AMAX", "")
        admin.transfer(s1, "20000.00000000 METH", "")
        admin.transfer(s1, "20000.00000000 MBTC", "")
        admin.transfer(s1, "20000.000000 MUSDT", "")
        return s1
    
if __name__ == "__main__":
    unittest.main()
