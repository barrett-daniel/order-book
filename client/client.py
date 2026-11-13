import socket
import time
import threading

from random_order_generator import RandomOrderGenerator
from order_types import *
from constants import *

def pack_request(order: OrderRequest) -> bytes:
    """Converts an order request into a list of bytes to be sent over the network"""
    byte_four = (order.operation & 0xF) | ((order.side & 0xF) << 4)
    byte_five = (order.type & 0xF) | ((order.tif & 0xF) << 4)
    return REQUEST_FMT.pack(MAGIC, byte_four, byte_five, order.price, order.quantity, order.order_id)

def unpack_response(data: bytes) -> Trade:
    """Converts a list of bytes into the encoded trade structure"""
    magic, buy_id, sell_id, quantity, price = struct.unpack_from(RESPONSE_FMT, data, 0)
    if magic != MAGIC: raise ValueError(f"Invalid Magic when unpacking response: {magic}")
    return Trade(buy_id, sell_id, quantity, price)

print_lock = threading.Lock()
def safe_print(msg: str) -> None:
    with print_lock:
        print(msg, flush=True)

class Client:
    def __init__(self):
        self.sock: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((IP_ADDR, PORT))
        self.order_gen = RandomOrderGenerator()
        self.orders: list[Order] = []

        self.should_close = False
        self.receiver_thread = threading.Thread(target=self._receive_loop, daemon=True)
        self.receiver_thread.start()

    def _receive_loop(self) -> None:
        safe_print("Starting Client Loop")
        while not self.should_close:
            data = self.sock.recv(RESPONSE_SIZE)

            # check to see if the message received is the client acknowledgment message
            # with the client's id
            if len(data) == 12:
                _, client_id = struct.unpack_from(CLIENT_ACCEPT_FMT, data, 0)
                safe_print(f"Received:    allocated client id#{client_id}")
                continue

            # handle the otherwise trade data response
            trade = unpack_response(data)
            self.process_trade(trade)
            safe_print(f"Received:    {trade}")

    def send_random_data(self) -> None:
        """Generates a random order request and sends it to the server"""
        order_request = self.order_gen.get_order_request(self.orders)
        safe_print(f"Sending:     {order_request}")
        self.sock.sendall(pack_request(order_request))

    def process_trade(self, trade: Trade):
        """Updates the client internal state based upon the trades received"""
        for order in list(self.orders): # copy as its mutated whilst iterating
            if order.id == trade.order_id:
                order.quantity -= trade.quantity
                if order.quantity <= 0: self.orders.remove(order)

    def close(self):
        self.should_close = True
        self.sock.shutdown(socket.SHUT_RDWR)
        self.sock.close()
        self.receiver_thread.join()


if __name__ == "__main__":
    client: Client = Client()
    for _ in range(1000): # send 1000 orders
        client.send_random_data()
        time.sleep(SLEEP_INTERVAL)
