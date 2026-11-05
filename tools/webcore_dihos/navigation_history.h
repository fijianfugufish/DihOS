#pragma once
#include "BackForwardClient.h"
#include "HistoryItem.h"
#include <wtf/Vector.h>
namespace DihosNavigation {
class NavigationHistory final : public WebCore::BackForwardClient {
public:
    static Ref<NavigationHistory> create(){return adoptRef(*new NavigationHistory);}
    void addItem(Ref<WebCore::HistoryItem>&& item) final {
        while(m_items.size()>m_current+1)m_items.removeLast();
        if(m_items.size()==64){m_items.removeAt(0);if(m_current)--m_current;}
        m_items.append(WTF::move(item));m_current=m_items.size()-1;
    }
    void setChildItem(WebCore::BackForwardFrameItemIdentifier,Ref<WebCore::HistoryItem>&& item) final {
        if(!m_items.isEmpty())m_items[m_current]->setChildItem(WTF::move(item));
    }
    void goToItem(WebCore::HistoryItem& item) final {
        for(size_t i=0;i<m_items.size();++i)if(m_items[i].get()==item){m_current=i;break;}
    }
    Vector<Ref<WebCore::HistoryItem>> allItems(WebCore::FrameIdentifier) final {return m_items;}
    RefPtr<WebCore::HistoryItem> itemAtIndex(int distance,WebCore::FrameIdentifier) final {
        auto index=static_cast<int64_t>(m_current)+distance;
        if(index<0 || static_cast<uint64_t>(index)>=m_items.size())return nullptr;
        return m_items[static_cast<size_t>(index)].ptr();
    }
    unsigned backListCount() const final {return m_items.isEmpty()?0:m_current;}
    unsigned forwardListCount() const final {return m_items.isEmpty()?0:m_items.size()-m_current-1;}
    bool containsItem(const WebCore::HistoryItem& item) const final {
        for(auto& entry:m_items)if(entry.get()==item)return true;return false;
    }
    void close() final {m_items.clear();m_current=0;}
private:
    Vector<Ref<WebCore::HistoryItem>> m_items;
    size_t m_current=0;
};
}
