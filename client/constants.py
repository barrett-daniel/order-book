import struct

# network constants
# =====================================
IP_ADDR: str = "127.0.0.1"
PORT: int = 8080

# configuration constants
# =====================================
SLEEP_INTERVAL: float = 0.3
MIN_INT_RNG_RANGE: int = 75
MAX_INT_RNG_RANGE: int = 125

# request / response packet constants
# =====================================
MAGIC = b"%MOB"
REQUEST_FMT = struct.Struct(">4sBBqQQ") # magic, operation/side, order_type/tif, price, quantity, order_id

RESPONSE_FMT = "!4sQQQq"  # buy_id, sell_id, quantity, price
RESPONSE_SIZE = struct.calcsize(RESPONSE_FMT)

CLIENT_ACCEPT_FMT = "!4sQ"  # client_id