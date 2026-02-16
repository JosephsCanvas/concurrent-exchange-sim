/**
 * @file replay_from_csv.cpp
 * @brief Replay orders from a CSV file through the exchange
 * 
 * CSV Format:
 *   type,order_id,trader_id,side,price,qty
 *   L,1,0,B,10000,100       (NewLimit, Buy)
 *   L,2,1,S,10100,50        (NewLimit, Sell)
 *   C,1,,,,                 (Cancel order 1)
 *   M,2,,,,75               (Modify order 2 qty to 75)
 */

#include <ces/common/types.hpp>
#include <ces/common/time.hpp>
#include <ces/lob/order_book.hpp>
#include <ces/lob/order.hpp>
#include <ces/engine/accounts.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ces;

struct CsvOrder {
    OrderType type;
    OrderId order_id;
    TraderId trader_id;
    Side side;
    Price price;
    Qty qty;
};

std::vector<CsvOrder> parse_csv(const std::string& filename) {
    std::vector<CsvOrder> orders;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file: " << filename << "\n";
        return orders;
    }
    
    std::string line;
    std::getline(file, line);  // Skip header
    
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        
        while (std::getline(ss, token, ',')) {
            tokens.push_back(token);
        }
        
        if (tokens.size() < 1) continue;
        
        CsvOrder order;
        
        // Parse type
        char type_char = tokens[0][0];
        switch (type_char) {
            case 'L': order.type = OrderType::NewLimit; break;
            case 'M': 
                if (tokens[0] == "M") order.type = OrderType::Modify;
                else order.type = OrderType::NewMarket;
                break;
            case 'C': order.type = OrderType::Cancel; break;
            default: continue;
        }
        
        // Parse order_id
        if (tokens.size() > 1 && !tokens[1].empty()) {
            order.order_id = OrderId{std::stoull(tokens[1])};
        }
        
        // Parse trader_id
        if (tokens.size() > 2 && !tokens[2].empty()) {
            order.trader_id = TraderId{static_cast<std::uint32_t>(std::stoul(tokens[2]))};
        }
        
        // Parse side
        if (tokens.size() > 3 && !tokens[3].empty()) {
            order.side = (tokens[3][0] == 'B') ? Side::Buy : Side::Sell;
        }
        
        // Parse price
        if (tokens.size() > 4 && !tokens[4].empty()) {
            order.price = Price{std::stoll(tokens[4])};
        }
        
        // Parse qty
        if (tokens.size() > 5 && !tokens[5].empty()) {
            order.qty = Qty{std::stoll(tokens[5])};
        }
        
        orders.push_back(order);
    }
    
    return orders;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <csv_file>\n";
        std::cout << "\nCSV Format:\n";
        std::cout << "  type,order_id,trader_id,side,price,qty\n";
        std::cout << "  L,1,0,B,10000,100    (NewLimit Buy)\n";
        std::cout << "  L,2,1,S,10100,50     (NewLimit Sell)\n";
        std::cout << "  C,1,,,,              (Cancel)\n";
        std::cout << "  M,2,,,,75            (Modify qty)\n";
        return 1;
    }
    
    std::string filename = argv[1];
    std::cout << "Reading orders from: " << filename << "\n";
    
    auto csv_orders = parse_csv(filename);
    std::cout << "Parsed " << csv_orders.size() << " orders\n\n";
    
    // Create order book
    OrderBook book(100000, 1024);
    Accounts accounts(100);
    
    // Set up trade callback
    std::uint64_t trade_count = 0;
    std::uint64_t trade_volume = 0;
    
    book.set_trade_callback([&](const Trade& trade) {
        std::cout << "  TRADE: " << trade.qty.get() << " @ " << trade.price.get()
                  << " (maker=" << trade.maker_order_id.get() 
                  << ", taker=" << trade.taker_order_id.get() << ")\n";
        ++trade_count;
        trade_volume += trade.qty.get();
    });
    
    // Process orders
    Timestamp start = now_ns();
    
    for (const auto& csv_order : csv_orders) {
        // Ensure account exists
        accounts.get_or_create(csv_order.trader_id, 1'000'000'000);
        
        OrderResponse response;
        
        switch (csv_order.type) {
            case OrderType::NewLimit:
                std::cout << "ADD LIMIT: id=" << csv_order.order_id.get()
                          << " " << to_string(csv_order.side)
                          << " " << csv_order.qty.get() << " @ " << csv_order.price.get() << "\n";
                response = book.add_limit(
                    csv_order.order_id, csv_order.trader_id,
                    csv_order.side, csv_order.price, csv_order.qty
                );
                break;
                
            case OrderType::NewMarket:
                std::cout << "ADD MARKET: id=" << csv_order.order_id.get()
                          << " " << to_string(csv_order.side)
                          << " " << csv_order.qty.get() << "\n";
                response = book.add_market(
                    csv_order.order_id, csv_order.trader_id,
                    csv_order.side, csv_order.qty
                );
                break;
                
            case OrderType::Cancel:
                std::cout << "CANCEL: id=" << csv_order.order_id.get() << "\n";
                response = book.cancel(csv_order.order_id);
                break;
                
            case OrderType::Modify:
                std::cout << "MODIFY: id=" << csv_order.order_id.get()
                          << " new_qty=" << csv_order.qty.get() << "\n";
                response = book.modify(csv_order.order_id, csv_order.qty, csv_order.price);
                break;
        }
        
        std::cout << "  -> " << to_string(response.result) << "\n";
    }
    
    Timestamp end = now_ns();
    double elapsed_ms = static_cast<double>(end - start) / 1e6;
    
    // Print summary
    std::cout << "\n=== Replay Summary ===\n";
    std::cout << "Orders processed: " << csv_orders.size() << "\n";
    std::cout << "Trades executed:  " << trade_count << "\n";
    std::cout << "Trade volume:     " << trade_volume << "\n";
    std::cout << "Elapsed time:     " << elapsed_ms << " ms\n";
    std::cout << "Throughput:       " << static_cast<std::uint64_t>(csv_orders.size() * 1000.0 / elapsed_ms)
              << " orders/sec\n";
    
    std::cout << "\n=== Final Book State ===\n";
    std::cout << "Active orders: " << book.order_count() << "\n";
    std::cout << "Bid levels:    " << book.bid_levels() << "\n";
    std::cout << "Ask levels:    " << book.ask_levels() << "\n";
    
    auto best_bid = book.best_bid();
    auto best_ask = book.best_ask();
    if (best_bid) std::cout << "Best bid:      " << best_bid->get() << "\n";
    if (best_ask) std::cout << "Best ask:      " << best_ask->get() << "\n";
    if (auto spread = book.spread()) std::cout << "Spread:        " << *spread << "\n";
    
    return 0;
}
