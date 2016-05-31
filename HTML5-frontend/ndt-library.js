/**
 * @fileoverview This javascript code includes an NDT object that contains the
 * methods that a customized front end implemetation of the NDT test could
 * utilize.  The NDT object is self invoked when the library is included on a
 * page, however, the frontend can customize certain properties when the
 * interaction is initialized with the startTest method.
 *
 * Dependencies: ndt-wrapper.js, ndt-browser-client.js
 */

/*jslint bitwise: true, browser: true, indent: 2, nomen: true */
/*global NDT, NDTjs, NDTWrapper*/

'use strict';

/**
  * @private
  * @constructor
  */
function NDTLibjs() {

  // Look up a suitable NDT server using mlab-ns. The ndtServer property is
  //  used by the backend javascript to determine which server to test
  //  against.
  /** @private */
  this.getServer_();

  // Initializes simulate property to false.  Can be changed upon starting a
  // test in the testServer method
  /** @private */
  this.simulate_ = false;

  // STATUS VARIABLES
  /** @private */
  this.use_websocket_client_ = false;

  /** @private */
  this.websocket_client_ = null;

}

/**
 * Operates on an instance of NDT to begin executing a test.
 * @private
 * @return - {object} Returns the protocol or an error if no ndtServer set.
 */
NDTLibjs.prototype.startTest_ = function () {
  try {
    if (this.ndtServer) {
      this.use_websocket_client_ = this.checkInstalledPlugins_();
      this.websocket_client_ = this.createBackend_(this.use_websocket_client_);
      this.testNDT_().run_test(this.ndtServer);
      return this.websocket_client_;
    }
    throw new Error("No MLab Server could be found.");
  } catch (e) {
    return e;
  }
};

/**
 * Operates on an instance of NDT to determine if the browser supports use of
 * websockets.
 * @private
 * @return - {bool} True if browser supports websockets, false if not
 */
NDTLibjs.prototype.checkInstalledPlugins_ = function () {
  try {
    var ndt_js = new NDTjs();
    return ndt_js.checkBrowserSupport();
  } catch (e) {
    return false;
  }
};

/**
 * Creates the backend for the NDT object.  Tries to create websocket backend
 * if supported, falls back to java applet if websocket not available
 * @private
 * @param - {bool} True, if browser can use websockets, false if not
 * @return - {object} If Java is to be used returns HTML Entity, otherwise
 *     returns ndtServer
 */
NDTLibjs.prototype.createBackend_ = function (use_ws) {
  var app;
  if (use_ws) {
    app = new NDTWrapper(this.ndtServer);
  } else {
    app = document.createElement('applet');
    app.id = 'NDT';
    app.name = 'NDT';
    app.archive = 'Tcpbw100.jar';
    app.code = 'edu.internet2.ndt.Tcpbw100.class';
    app.width = '600';
    app.height = '10';
  }
  return app;
};

/**
 * Determines where to run the tests.  If against the websocket or Java
 * applet
 * @private
 * @return - {object} websocket client or java applet element id
 */
NDTLibjs.prototype.testNDT_ = function () {
  if (this.websocket_client_) {
    return this.websocket_client_;
  }
  return ('#NDT');
};

/**
 * AJAX call to determine what server to run tests against.
 * @private
 * @return - none, sets object property for server
 */
NDTLibjs.prototype.getServer_ = function () {
  var resp,
    mlabNsService = ('https:' === location.protocol) ? 'ndt_ssl' : 'ndt',
    mlabNsUrl = 'https://mlab-ns.appspot.com/',
    that = this,
    NDTAjax = new XMLHttpRequest();

  NDTAjax.open('GET', mlabNsUrl + mlabNsService + '?format=json', true);
  NDTAjax.onreadystatechange = function () {
    // completed and successful
    if (NDTAjax.readyState === 4 && NDTAjax.status === 200) {
      resp = JSON.parse(NDTAjax.responseText);
      console.log('Using NDT server: ' + resp.fqdn);
      that.ndtServer = resp.fqdn;
    } else if (NDTAjax.readyState === 4 && NDTAjax.status !== 200) {
      console.log('mlab-ns lookup failed: ' + NDTAjax.status);
      that.ndtServer = false;
    }
  };
  NDTAjax.send();
};


// Monitoring Test Functions

/**
 * Gets the test status of the current NDT test
 * @private
 * @return - {string} status of current test
 */
NDTLibjs.prototype.testStatus_ = function () {
  return this.testNDT_().get_status();
};

/**
 * Gets error messages of the current NDT test
 * @private
 * @return - {string} error message if occurred
 */
NDTLibjs.prototype.testError_ = function () {
  return this.testNDT_().get_errmsg();
};

/**
 * Gets hostname of server running test
 * @private
 * @return - {string} hostname of server
 */
NDTLibjs.prototype.remoteServer_ = function () {
  if (this.simulate) { return '0.0.0.0'; }
  return this.testNDT_().get_host();
};

/**
 * Gets the Upload speed of the current NDT test
 * @private
 * @return - {float} speed of uploading to server
 */
NDTLibjs.prototype.uploadSpeed_ = function (raw) {
  var speed;

  if (this.simulate) { return 0; }
  speed = this.testNDT_().getNDTvar('ClientToServerSpeed');
  return raw ? speed : parseFloat(speed);
};

/**
 * Gets the Download speed of the current NDT test
 * @private
 * @return - {float} speed of downloading from server
 */
NDTLibjs.prototype.downloadSpeed_ = function () {
  if (this.simulate) { return 0; }
  return parseFloat(this.testNDT_().getNDTvar('ServerToClientSpeed'));
};

/**
 * Gets the Average round trip speed of the current NDT test
 * @private
 * @return - {float} speed of round trip
 */
NDTLibjs.prototype.averageRoundTrip_ = function () {
  if (this.simulate) { return 0; }
  return parseFloat(this.testNDT_().getNDTvar('avgrtt'));
};

/**
 * Gets the Jitter value of the current NDT test
 * @private
 * @return - {float} jitter value
 */
NDTLibjs.prototype.jitter_ = function () {
  if (this.simulate) { return 0; }
  return parseFloat(this.testNDT_().getNDTvar('Jitter'));
};

/**
 * Gets the current top speed of tests
 * @private
 * @return - {float} speed limit value during current test
 */
NDTLibjs.prototype.speedLimit_ = function () {
  if (this.simulate) { return 0; }
  return parseFloat(this.testNDT_().get_PcBuffSpdLimit());
};

// instantiate NDT object
var NDT = {};
NDT = new NDTLibjs();