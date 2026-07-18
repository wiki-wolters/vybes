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

  export {
    throttleAndDebounce,
    debounce,
    asyncDebounce
  }