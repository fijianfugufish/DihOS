// Language/runtime probe only: this intentionally does not claim DOM support.
const checks = {};
function check(name, value) { checks[name] = !!value; if (!value) throw new Error(name); }
check('arithmetic', [1, 2, 3].map(x => x * x).reduce((a, b) => a + b, 0) === 14);
check('typed_arrays', new Uint32Array([10, 20, 30])[1] === 20);
check('unicode', Array.from('DihOS 🐟').length === 7);
check('map_set', new Set([1, 1, 2]).size === 2 && new Map([['ok', 42]]).get('ok') === 42);
check('json', JSON.parse(JSON.stringify({ value: 42 })).value === 42);
check('intl', new Intl.NumberFormat('en-US').format(1234) === '1,234');
Promise.resolve(42).then(value => {
    check('promise_jobs', value === 42);
    print('DIHOS_JSC_RESULT ' + JSON.stringify({
        checks, language_pass: Object.values(checks).every(Boolean),
        browser: { document: typeof document, fetch: typeof fetch,
                   mediaSource: typeof MediaSource, timers: typeof setTimeout }
    }));
});
