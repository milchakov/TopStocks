#ifndef stock_h
#define stock_h

#include <functional>

struct Stock
{
    Stock()
        : m_id(0)
        , m_firstPrice(0)
        , m_lastPrice(0)
        , m_change(0.0)
    {}

    Stock(int id, double price)
        : m_id(id)
        , m_firstPrice(price)
        , m_lastPrice(price)
        , m_change(0.0)
    {}

    int GetID() const
    {
        return m_id;
    }

    double GetFirstPrice() const
    {
        return m_firstPrice;
    }

    double GetLastPrice() const
    {
        return m_lastPrice;
    }

    double GetChange() const
    {
        return m_change;
    }

    void SetPrice(double price)
    {
        m_lastPrice = price;
        RecalcChange();
    }

    friend bool operator == (const Stock &lhs, const Stock &rhs)
    {
        return lhs.m_id == rhs.m_id;
    }

    friend bool operator < (const Stock &lhs, const Stock &rhs)
    {
        return lhs.m_change < rhs.m_change;
    }

private:

    void RecalcChange()
    {
        m_change = ((m_lastPrice - m_firstPrice) / m_firstPrice) * 100.0;
    }

    int m_id;
    double m_firstPrice;
    double m_lastPrice;
    double m_change;
};


#endif  //stock_h


