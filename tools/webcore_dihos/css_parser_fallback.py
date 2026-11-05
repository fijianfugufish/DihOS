"""Exact, bounded allocation-failure guards for the pinned CSSParser source."""
def patch_css_token_budget(text):
    old='m_tokenizer(wrapper ? CSSTokenizer::tryCreate(string, *wrapper) : CSSTokenizer::tryCreate(string))'
    # Large real sheets (YouTube's main CSS is ~3.8 MB) exceed the old 16 MiB
    # pessimistic token reservation. Stay below the 64 MiB owned-pool ceiling.
    new='m_tokenizer(string.length() / 3 > (48u * 1024u * 1024u - 65536u) / sizeof(CSSParserToken) ? std::unique_ptr<CSSTokenizer>{} : wrapper ? CSSTokenizer::tryCreate(string, *wrapper) : CSSTokenizer::tryCreate(string))'
    assert text.count(old)==1,'Pinned tokenizer constructor changed'
    return text.replace(old,new)

def patch_css_parser(text):
    patches=[
      ('    parser.consumeDeclarationValue(parser.tokenizer()->tokenRange(), propertyID, important, ruleType);',
       '    if (!parser.tokenizer())\n        return ParseResult::Error;\n    parser.consumeDeclarationValue(parser.tokenizer()->tokenRange(), propertyID, important, ruleType);'),
      ('    auto range = parser.tokenizer()->tokenRange();',
       '    if (!parser.tokenizer())\n        return ParseResult::Error;\n    auto range = parser.tokenizer()->tokenRange();'),
      ('    CSSParser parser(context, string);\n    parser.consumeDeclarationList(parser.tokenizer()->tokenRange(), StyleRuleType::Style);',
       '    CSSParser parser(context, string);\n    if (!parser.tokenizer())\n        return createStyleProperties(parser.topContext().m_parsedProperties, context.mode);\n    parser.consumeDeclarationList(parser.tokenizer()->tokenRange(), StyleRuleType::Style);'),
      ('    CSSParser parser(context, string);\n    auto ruleType =',
       '    CSSParser parser(context, string);\n    if (!parser.tokenizer())\n        return false;\n    auto ruleType ='),
      ('    CSSParserTokenRange range = parser.tokenizer()->tokenRange();',
       '    if (!parser.tokenizer())\n        return nullptr;\n    CSSParserTokenRange range = parser.tokenizer()->tokenRange();'),
      ('    CSSParser parser(context, string, &styleSheet, nullptr);',
       '    CSSParser parser(context, string, &styleSheet, nullptr);\n    if (!parser.tokenizer()) {\n        styleSheet.setHasSyntacticallyValidCSSHeader(false);\n        return;\n    }'),
      ('    CSSParser parser(context, declaration, nullptr, wrapper.ptr());',
       '    CSSParser parser(context, declaration, nullptr, wrapper.ptr());\n    if (!parser.tokenizer())\n        return;'),
      ('    CSSParser parser(context, string, &styleSheet, wrapper.ptr());',
       '    CSSParser parser(context, string, &styleSheet, wrapper.ptr());\n    if (!parser.tokenizer()) {\n        styleSheet.setHasSyntacticallyValidCSSHeader(false);\n        return;\n    }'),
    ]
    for old,new in patches:
        assert text.count(old)==1,old
        text=text.replace(old,new)
    return patch_css_token_budget(text)
