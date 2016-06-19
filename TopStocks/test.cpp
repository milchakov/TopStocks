#include "TopStocks.h"

#include <iostream>
#include <algorithm>
#include <cstdlib>

#include <conio.h>

class SomeListenner : public TopStocksListenner
{
public:

    void TopGainersChanged(TopStockIterator first, TopStockIterator last) override
    {
        std::cout << "\n Top gainers \n";
        std::for_each(first, last, [](const Stock & stock)
        {
            std::cout << "Id = " << stock.GetID() << "; FirstPrice = " << stock.GetFirstPrice() << "; LastPrice = " << stock.GetLastPrice() << "; Change = " << stock.GetChange() << ";\n";
        });
    };

    void TopLosesChanged(TopStockIterator first, TopStockIterator last) override
    {
        std::cout << "\n Top loses \n";
        std::for_each(first, last, [](const Stock & stock)
        {
            std::cout << "Id = " << stock.GetID() << "; FirstPrice = " << stock.GetFirstPrice() << "; LastPrice = " << stock.GetLastPrice() << "; Change = " << stock.GetChange() << ";\n";
        });
    };
};

int main(int argc, char* argv[])
{
    srand((unsigned int)time(NULL));
    SomeListenner listenner;
    TopStocks <10> topStocks;
    topStocks.Register(&listenner);
    
    size_t id = 1;

    for(size_t i = 0; i < 10000; ++i)
    {
        topStocks.OnQuote(id, ((double)(rand() % 1000) + 1.e-6) / ((double)(rand() % 1000) + 1.e-6));
        ++id;
        if (90 < id)
        {
            id = 1;
        }
    }

    _getch();
	return 0;
}

