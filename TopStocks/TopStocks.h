#ifndef top_stocks_h
#define top_stocks_h

#include <unordered_map>
#include <set>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <cassert>

#include "Stock.h"

//Notifier - class for listeners registration and notification.
template <class T>
class Notifier
{
public:

    Notifier()
    {}

    virtual ~Notifier() 
    {
        std::lock_guard <std::mutex> lck(m_mtx);
        m_listeners.clear();
    }  

    void Register(T *listener)
    {
        std::lock_guard <std::mutex> lck(m_mtx);
        if(nullptr != listener)
        {
            m_listeners.push_back(listener);
        }
    }

    void Unregister(T *listener)
    {
        std::lock_guard <std::mutex> lck(m_mtx);
        m_listeners.erase(std::remove(m_listeners.begin(), m_listeners.end(), listener), m_listeners.end());
    }

    template <class fn>
    void NotifyAny( fn func)
    {
        std::lock_guard <std::mutex> lck(m_mtx);   // should be changed to boost::shared_lock in production

        for(auto &listener_ptr : m_listeners)
        {
            if(nullptr != listener_ptr)
            {
                try
                {
                    func(*listener_ptr);
                }
                catch(...)
                {
                    std::cout << "\nNotifier - exception was thrown from user-defined event handler code, "
                        "pointer to user object = " << listener_ptr;
                    assert(false);
                }
            }
        }
    }

private:

    // default constructible only
    Notifier(const Notifier &);             // = delete;       
    Notifier(Notifier &&);                  // = delete;
    Notifier & operator=(const Notifier &); // = delete;
    Notifier & operator=(Notifier &&);      // = delete;

    std::mutex m_mtx;   // should be changed to boost::shared_mutex in production
    std::vector<T*> m_listeners;
};

//=============================================================================================
//---------------------------------------------------------------------------------------------
//=============================================================================================
typedef std::vector <Stock>::const_iterator TopStockIterator;

//TopStocksListenner - base class for event listeners
class TopStocksListenner
{
public:

    virtual void TopGainersChanged(TopStockIterator first, TopStockIterator last) {};
    virtual void TopLosesChanged(TopStockIterator first, TopStockIterator last) {};
};

//=============================================================================================
//---------------------------------------------------------------------------------------------
//=============================================================================================
//TopStocksEvent - class which contains last unread event.
//Behavior like multithreaded queue with fixed size 1.
template <int TTopStocksSize>
class TopStocksEvent
{
public:
    TopStocksEvent()
        : m_isEmpty(true)
    {}

    ~TopStocksEvent()
    {
        ClearNotify();
    }

    template <class ForwardIterator>
    void Set(ForwardIterator first, ForwardIterator last)
    {
        std::lock_guard <std::mutex> lck(m_mtx);

        try
        {
            m_eventData.reset(new std::vector <Stock>(TTopStocksSize)); // we need to contain copy of data to support async work
        }
        catch(const std::bad_alloc &ex)
        {
            std::cout << "\nTopStocksEvent - the memory is not enough to allocate data in event, "
                         "array size = " << TTopStocksSize << ". \nEvent will be skipped!"
                         "\nException info: " << ex.what();
            return;
        }
        

        std::copy(first,last, m_eventData->begin());

        m_isEmpty = false;
        m_event.notify_all();
    }

    std::shared_ptr <std::vector <Stock>> Get()
    {
        std::unique_lock <std::mutex> ulck(m_mtx);
        if(m_isEmpty)
        {
            m_event.wait(ulck);
        }

        if(!m_isEmpty)   // check when event cleared (thread notified but data is empty)
        {
            m_isEmpty = true;
            return m_eventData;
        }

        return std::shared_ptr <std::vector <Stock>> ();    
    }

    void ClearNotify() // clear and notify reading thread
    {
        std::lock_guard <std::mutex> lck(m_mtx);
        m_eventData.reset();
        m_isEmpty = true;
        m_event.notify_all();
    }

private:

    // default constructible only
    TopStocksEvent(const TopStocksEvent &);             // = delete;       
    TopStocksEvent(TopStocksEvent &&);                  // = delete;
    TopStocksEvent & operator=(const TopStocksEvent &); // = delete;
    TopStocksEvent & operator=(TopStocksEvent &&);      // = delete;

    std::mutex m_mtx;
    std::condition_variable m_event;
    bool m_isEmpty;

    // it is vector to support iterators in events, 
    // but it could be array if we need to use less of memory
    std::shared_ptr <std::vector <Stock>> m_eventData;
};
//=============================================================================================
//---------------------------------------------------------------------------------------------
//=============================================================================================
//TopStocks - 
template <int TTopStocksSize>
class TopStocks : public Notifier <TopStocksListenner>
{
public:

    TopStocks()
    {
    }

    ~TopStocks()
    {
        m_runFlag.store(false);

        m_topGainersEvent.ClearNotify();
        m_topLosesEvent.ClearNotify();

        if(m_gainersNotifyThr.joinable())
        {
            m_gainersNotifyThr.join();
        }

        if(m_losesNotifyThr.joinable())
        {
            m_losesNotifyThr.join();
        }
    }

    void OnQuote(int stock_id, double price)
    {
        std::lock_guard <std::mutex> lock(m_mtx);

        std::call_once(m_onceFlag, &TopStocks::InitThreads, this);                      // lazy initialization on first portion of data

        auto currentStockIt = m_stocksByID.find(stock_id);                              // find stock by ID with previous price
        if(m_stocksByID.end() == currentStockIt)
        {
            auto emplaceResult = m_stocksByID.emplace(stock_id, Stock(stock_id, price));
            currentStockIt = emplaceResult.first;

            EmplaceAndNotify(currentStockIt->second);
        }
        else
        {  
            auto range = m_sortedStocks.equal_range(currentStockIt->second);            // find range of stocks with equivalent Change value

            if(m_sortedStocks.end() != range.first)
            {
                for(auto it = range.first; it != range.second; ++it)                    // find stock by ID
                {
                    if(currentStockIt->second == *it)
                    {
                        m_sortedStocks.erase(it);                                       // remove stock with previous price
                        currentStockIt->second.SetPrice(price);                         // and insert with new price
                        EmplaceAndNotify(currentStockIt->second);
                        return;
                    }
                }
            }
        }
    }
    

private:

    void EmplaceAndNotify(const Stock &stock)
    {
        auto emplasedIt = m_sortedStocks.emplace(stock);

        if(m_sortedStocks.size() <= TTopStocksSize * 2)     //if container length less than topgainers + toploses length 
        {
            PushGainersEvent();
            PushLosesEvent();
        }
        else
        {
            if(emplasedIt == m_sortedStocks.begin())        
            {
                PushLosesEvent();
                return;
            }

            auto decrementIt = emplasedIt;
            auto incrementIt = emplasedIt;
            for(size_t i = 0; i < TTopStocksSize - 1; ++i)  // just check if new stock near begin or end
            {
                if(--decrementIt == m_sortedStocks.begin())
                {
                    PushLosesEvent();
                    return;
                }
                if(++incrementIt == m_sortedStocks.end())
                {
                    PushGainersEvent();
                    return;
                }
            }

            if(++incrementIt == m_sortedStocks.end())
            {
                PushGainersEvent();
                return;
            }
        }
    }

    void PushGainersEvent()
    {
        auto last = InitLast(m_sortedStocks.crbegin(), m_sortedStocks.crend()); //reset last event
        m_topGainersEvent.Set(m_sortedStocks.crbegin(), last);
    }

    void PushLosesEvent()
    {
        auto last = InitLast(m_sortedStocks.cbegin(), m_sortedStocks.cend());   //reset last event
        m_topLosesEvent.Set(m_sortedStocks.cbegin(), last);
    }

    template <typename BidirIterator>
    BidirIterator InitLast(BidirIterator first, BidirIterator last)
    {
        if(m_sortedStocks.size() < TTopStocksSize)
        {
            return last;
        }

        std::advance(first, TTopStocksSize);

        return first;
    }

    void InitThreads()
    {
        m_runFlag.store(true);
        m_gainersNotifyThr = std::thread(
            &TopStocks::WaitAndNotify <decltype(&TopStocksListenner::TopGainersChanged)>, 
            this, std::ref(m_topGainersEvent), &TopStocksListenner::TopGainersChanged);

        m_losesNotifyThr = std::thread(
            &TopStocks::WaitAndNotify <decltype(&TopStocksListenner::TopLosesChanged)>, 
            this, std::ref(m_topLosesEvent), &TopStocksListenner::TopLosesChanged);
    }

    template <class fn>
    void WaitAndNotify(TopStocksEvent <TTopStocksSize> &event, fn func)
    {
        while(m_runFlag)
        {
            auto eventDataPtr = event.Get();

            if(eventDataPtr)
            {
                NotifyAny(std::bind(func, std::placeholders::_1, eventDataPtr->cbegin(), eventDataPtr->cend()));
            }
        }
    }

    std::mutex m_mtx;

    // main algorithm containers
    std::unordered_map <int, Stock> m_stocksByID;   
    std::multiset <Stock> m_sortedStocks;           

    //async events support
    TopStocksEvent <TTopStocksSize> m_topGainersEvent;
    TopStocksEvent <TTopStocksSize> m_topLosesEvent;

    std::thread m_gainersNotifyThr;
    std::thread m_losesNotifyThr;

    std::atomic <bool> m_runFlag;
    std::once_flag m_onceFlag;
};

#endif  //top_stocks_h