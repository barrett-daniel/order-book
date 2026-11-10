import socket
import time
import random
import struct
from enum import IntEnum
from dataclasses import dataclass
from typing import ClassVar
import threading

# network constants
# =====================================
IP_ADDR: str = "127.0.0.1"
PORT: int = 8080

# configuration constants
# =====================================
SLEEP_INTERVAL: float = 1.5
MIN_INT_RNG_RANGE: int = 1
MAX_INT_RNG_RANGE: int = 100

# request / response packet constants
# =====================================
MAGIC = b"%MOB"
REQUEST_FMT = struct.Struct(">4sBBqQQ") # magic, operation/side, order_type/tif, price, quantity, order_id

RESPONSE_FMT = "!4sQQQq"  # buy_id, sell_id, quantity, price
RESPONSE_SIZE = struct.calcsize(RESPONSE_FMT)

# enum types
# =====================================

class OrderType(IntEnum):
    Market = 0
    Limit = 1


class Side(IntEnum):
    Buy = 0
    Sell = 1


class TimeInForce(IntEnum):
    IOC = 0
    FOK = 1
    GTC = 2


class OrderOperation(IntEnum):
    Add = 0
    Modify = 1
    Cancel = 2

# data types
# =====================================

@dataclass
class OrderRequest:
    curr_max_order_id: ClassVar[int] = 0
    operation: OrderOperation = OrderOperation.Add
    order_id: int = 0
    price: int = 0
    quantity: int = 0
    side: Side  = Side.Buy
    type: OrderType = OrderType.Market
    tif: TimeInForce = TimeInForce.IOC

    def __str__(self) -> str:
        match self.operation:
            case OrderOperation.Cancel: return f"OrderRequest : {self.operation.name:<8} {self.order_id:<8}"
            case OrderOperation.Modify: return f"OrderRequest : {self.operation.name:<8} {self.order_id:<8} {self.quantity:<8} @ {self.price:<8}"
            case OrderOperation.Add:    return f"OrderRequest : {self.operation.name:<8} #{self.order_id:<8}: {self.side.name:<4} {self.quantity:<8} {self.type.name:<8} @ {self.price:<8} {self.tif.name:<8}"

@dataclass
class Trade:
    order_id: int = 0
    other_client_id: int = 0
    quantity: int = 0
    price: int = 0

    def __str__(self) -> str:
        return f"Trade : id#{self.order_id:<8} other_client_id#{self.other_client_id:<8} {self.quantity:<8} @ {self.price:<8}"


# Network Packing
# =====================================

def pack_request(order: OrderRequest) -> bytes:
    byte_four = (order.operation & 0xF) | ((order.side & 0xF) << 4)
    byte_five = (order.type & 0xF) | ((order.tif & 0xF) << 4)
    return REQUEST_FMT.pack(MAGIC, byte_four, byte_five, order.price, order.quantity, order.order_id)

def unpack_response(data: bytes) -> Trade:
    magic, buy_id, sell_id, quantity, price = struct.unpack_from(RESPONSE_FMT, data, 0)
    if magic != MAGIC: raise ValueError(f"Invalid Magic when unpacking response: {magic}")

    return Trade(buy_id, sell_id, quantity, price)

# logic handlers
# =====================================

print_lock = threading.Lock()

def safe_print(msg: str) -> None:
    with print_lock:
        print(msg, flush=True)

class RandomOrderGenerator:
    def __init__(self):
        self.rng = random.Random()
        self.order_ids = []

    def get_order(self) -> OrderRequest:
        operation = OrderOperation.Add if len(self.order_ids) == 0 else self.rng.choices(list(OrderOperation), weights=[5,1,1])[0]

        side = self.rng.choice(list(Side))
        type = self.rng.choices(list(OrderType), weights=[1,5])[0]
        tif = self.rng.choices(list(TimeInForce), weights=[1, 1, 5])[0]

        price = 0 if type == OrderType.Market else self.rng.randint(MIN_INT_RNG_RANGE, MAX_INT_RNG_RANGE)
        quantity = self.rng.randint(MIN_INT_RNG_RANGE, MAX_INT_RNG_RANGE)

        order_id = 0
        if operation == OrderOperation.Add:
            order_id = OrderRequest.curr_max_order_id
            OrderRequest.curr_max_order_id += 1
            self.order_ids.append(order_id)
        else:
            order_id = self.rng.choice(self.order_ids)

        match operation:
            case OrderOperation.Cancel: return OrderRequest(operation=OrderOperation.Cancel, order_id=order_id)
            case OrderOperation.Modify: return OrderRequest(operation=OrderOperation.Modify, order_id=order_id, price=price, quantity=quantity)
            case OrderOperation.Add:    return OrderRequest(operation=OrderOperation.Add,    order_id=order_id, price=price, quantity=quantity, side=side, type=type, tif=tif)

    def add_order_id(self, order_id: int):
        self.order_ids.append(order_id)

CLIENT_ACCEPT_FMT = "!4sQ"  # client_id

class Client:
    def __init__(self):
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((IP_ADDR, PORT))
        self.order_gen = RandomOrderGenerator()

        self.should_close = False
        self._rx_thread = threading.Thread(target=self._receive_loop, daemon=True)
        self._rx_thread.start()

    def _receive_loop(self) -> None:
        safe_print("Starting Client Loop")
        while not self.should_close:
            data = self.sock.recv(RESPONSE_SIZE)

            if len(data) == 12:
                _, client_id = struct.unpack_from(CLIENT_ACCEPT_FMT, data, 0)
                safe_print(f"Received:    allocated client id#{client_id}")
                continue

            trade = unpack_response(data)
            safe_print(f"Received:    {trade}")

    def send_random_data(self) -> None:
        order = self.order_gen.get_order()
        safe_print(f"Sending:     {order}")
        self.sock.sendall(pack_request(order))

    def close(self):
        self.should_close = True
        self.sock.shutdown(socket.SHUT_RDWR)
        self.sock.close()
        self._rx_thread.join()


if __name__ == "__main__":
    client: Client = Client()
    for _ in range(100):
        client.send_random_data()
        time.sleep(SLEEP_INTERVAL)
