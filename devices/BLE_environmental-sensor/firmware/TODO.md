* Optimize BLE connection
  * Reduce TX Power to 0dBm
  * Increase connection interval, and maybe add some latency.
* Implement the RACP service
  * note: sl_bt indications have to be done from main loop or ble handler, not from ISR context
  * BLE: one indication flying. Should wait for ack event before sending the
    next one. Limit per client connection.
  * add a way to buffer the journal read ?, or buffer them manually? no pausing in ISR context !


* Refactor app and bt handling in two different thing.
