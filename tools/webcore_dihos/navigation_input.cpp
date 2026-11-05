#include "config.h"
#include "PlatformKeyboardEvent.h"
#include <wtf/text/WTFString.h>
namespace WebCore {
void PlatformKeyboardEvent::disambiguateKeyDownEvent(Type type,bool compatibility)
{
    ASSERT(m_type==Type::KeyDown);m_type=type;
    if(compatibility)return;
    if(type==Type::RawKeyDown){m_text=String();m_unmodifiedText=String();}
    else m_keyIdentifier=String();
}
}
