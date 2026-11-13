import random

from order_types import *
from constants import *

class RandomOrderGenerator:
    def __init__(self):
        self.rng = random.Random()

    def get_order_request(self, current_orders: list[Order]) -> OrderRequest:
        """Generates a random order request. note: current_orders is passed as a reference and can be mutated by the
        function call"""

        # first generate some random order data
        # some of the below use weighted random generation to bias the generation to order properties which result
        # in more resting orders, and hence populates the initial empty order book more quickly

        operation = OrderOperation.Add if len(current_orders) == 0 else self.rng.choices(list(OrderOperation), weights=[5,1,1])[0]

        side = self.rng.choice(list(Side))
        order_type = self.rng.choices(list(OrderType), weights=[1,5])[0]
        tif = self.rng.choices(list(TimeInForce), weights=[1, 1, 5])[0]

        price = 0 if order_type == OrderType.Market else self.rng.randint(MIN_INT_RNG_RANGE, MAX_INT_RNG_RANGE)
        quantity = self.rng.randint(MIN_INT_RNG_RANGE, MAX_INT_RNG_RANGE)

        order_id = 0
        random_order: Order | None = None
        if operation == OrderOperation.Add:
            order_id = OrderRequest.curr_max_order_id
            OrderRequest.curr_max_order_id += 1
        else:
            # if we're cancelling/modifying the order, get a reference to a random order to modify
            random_order = self.rng.choice(current_orders)
            assert random_order is not None
            order_id = random_order.id

        # generate the order request object and update the state of orders given to the function based on the operation
        match operation:
            case OrderOperation.Cancel:
                assert random_order is not None
                current_orders.remove(random_order)
                return OrderRequest(operation=OrderOperation.Cancel, order_id=order_id)

            case OrderOperation.Modify:
                assert random_order is not None
                order_idx = current_orders.index(random_order)
                current_orders[order_idx].price = price
                current_orders[order_idx].quantity = quantity
                if current_orders[order_idx].quantity == 0: current_orders.remove(random_order)
                return OrderRequest(operation=OrderOperation.Modify, order_id=order_id, price=price, quantity=quantity)

            case OrderOperation.Add:
                if tif == TimeInForce.GTC and order_type == OrderType.Limit:
                    current_orders.append(Order(type=order_type, side=side, id=order_id, price=price, quantity=quantity, tif=tif))
                return OrderRequest(operation=OrderOperation.Add,    order_id=order_id, price=price, quantity=quantity, side=side, type=order_type, tif=tif)
