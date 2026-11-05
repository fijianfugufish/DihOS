"""Keep WebCore's skipped-transition semantics on the DihOS software port."""
def patch_document(text):
    rule='Variant<Document::SkipTransition, Vector<AtomString>> Document::resolveViewTransitionRule()\n{\n'
    assert text.count(rule)==1
    text=text.replace(rule,rule+'''#if PLATFORM(DIHOS)
    // Cross-document snapshots require composited backing. Normal navigation
    // remains available when this port paints directly into a Cairo surface.
    if (!settings().acceleratedCompositingEnabled())
        return SkipTransition { };
#endif

''')
    start='    setActiveViewTransition(WTF::move(viewTransition));\n    scheduleRenderingUpdate(RenderingUpdateStep::PerformPendingViewTransitions);'
    assert text.count(start)==1
    text=text.replace(start,'''#if PLATFORM(DIHOS)
    // Skip before setActiveViewTransition(), which eagerly enables compositing
    // even when acceleratedCompositingEnabled is false. Use the engine's
    // normal skip algorithm: still queue the DOM update callback, reject ready
    // with AbortError, and settle updateCallbackDone/finished from its result.
    if (!settings().acceleratedCompositingEnabled()) {
        viewTransition->skipViewTransition(Exception { ExceptionCode::AbortError, "Skipping visual view transition on the software rendering backend."_s });
        return viewTransition;
    }
#endif

'''+start)
    return text
