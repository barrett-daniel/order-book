from enum import IntEnum
from dataclasses import dataclass
from typing import ClassVar


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
class Order:
    type: OrderType
    side: Side
    quantity: int
    price: int
    id: int
    tif: TimeInForce


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
            case OrderOperation.Cancel: return f"OrderRequest : {self.operation.name:<8} #{self.order_id:<8}"
            case OrderOperation.Modify: return f"OrderRequest : {self.operation.name:<8} #{self.order_id:<8} {self.quantity:<8} @ {self.price:<8}"
            case OrderOperation.Add:    return f"OrderRequest : {self.operation.name:<8} #{self.order_id:<8}: {self.side.name:<4} {self.quantity:<8} {self.type.name:<8} @ {self.price:<8} {self.tif.name:<8}"


@dataclass
class Trade:
    order_id: int = 0
    other_client_id: int = 0
    quantity: int = 0
    price: int = 0

    def __str__(self) -> str:
        return f"Trade        : id#{self.order_id:<8} other_client_id#{self.other_client_id:<8} {self.quantity:<8} @ {self.price:<8}"