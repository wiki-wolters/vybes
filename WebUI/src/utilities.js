function throttleAndDebounce(func, throttleWait = 500, debounceWait = 200) {
    let throttleTimeout;
    let debounceTimeout;
    let lastExecTime = 0;
    
    return function(...args) {
      const now = Date.now();
      
      // Clear any pending debounce
      clearTimeout(debounceTimeout);
      
      // Throttle: Execute immediately if enough time has passed
      if (now - lastExecTime >= throttleWait) {
        func.apply(this, args);
        lastExecTime = now;
        clearTimeout(throttleTimeout);
      } else if (!throttleTimeout) {
        // Schedule throttled execution
        throttleTimeout = setTimeout(() => {
          func.apply(this, args);
          lastExecTime = Date.now();
          throttleTimeout = null;
        }, throttleWait - (now - lastExecTime));
      }
      
      // Always schedule a debounced execution for the final save
      debounceTimeout = setTimeout(() => {
        func.apply(this, args);
        lastExecTime = Date.now();
      }, debounceWait);
    };
  }

  function debounce(func, wait = 200) {
    let timeout;
    return function(...args) {
      clearTimeout(timeout);
      timeout = setTimeout(() => {
        func.apply(this, args);
      }, wait);
    };
  }

  function asyncDebounce(func, wait = 200) {
    let timeout;
    let waiters = [];
    return function(...args) {
      return new Promise((resolve, reject) => {
        // Every caller gets settled: superseded calls share the final
        // invocation's result instead of leaving their promises pending
        // forever (which would hang any await on them).
        waiters.push({ resolve, reject });
        clearTimeout(timeout);
        timeout = setTimeout(async () => {
          const settled = waiters;
          waiters = [];
          try {
            const result = await func.apply(this, args);
            settled.forEach(w => w.resolve(result));
          } catch (error) {
            settled.forEach(w => w.reject(error));
          }
        }, wait);
      });
    };
  }

  const NBSP = '\u00a0'; // non-breaking space

  /**
   * The single formatter for numeric readouts (slider values, chips, pool
   * counts). Rounds to `decimals` without padding trailing zeros, keeps the
   * number glued to its unit so a readout never wraps mid-value, and
   * collapses Hz to kHz above 1 kHz the same way the EQ band chips do.
   */
  function formatValue(value, unit = '', decimals = 2) {
    if (typeof value !== 'number' || !Number.isFinite(value)) return `${value}`;
    const suffix = unit.trim();

    if (suffix === 'Hz' && Math.abs(value) >= 1000) {
      const kHz = value / 1000;
      return `${kHz.toFixed(Math.abs(value) < 10000 ? 2 : 1)}${NBSP}kHz`;
    }

    const rounded = parseFloat(value.toFixed(decimals));
    // Rounding -0.04 to one decimal must not read as "-0"
    const number = (rounded === 0 ? 0 : rounded)
      .toLocaleString(undefined, { maximumFractionDigits: decimals });

    if (!suffix) return number;
    // Percent reads as one token; every other unit takes a space
    if (suffix === '%') return `${number}%`;
    return `${number}${NBSP}${suffix}`;
  }

  export {
    throttleAndDebounce,
    debounce,
    asyncDebounce,
    formatValue
  }