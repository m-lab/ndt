/**
 * @fileoverview This javascript code includes an NDT object that contains the
 * methods that a customized front end implemetation of the NDT test could
 * utilize.  The NDT object is self invoked when the library is included on a
 * page, however, the frontend can customize certain properties when the
 * interaction is initialized with the startTest method.  
 *
 * Dependencies: ndt-wrapper.js, ndt-browser-client.js
 */


/**
 * Immediately Invoked Function Expression that creates NDT library object
 */
(function (window) {
  'use strict';

  /** @constructor */
  function NDTLibjs() {

    // Look up a suitable NDT server using mlab-ns. The ndtServer property is
    //  used by the backend javascript to determine which server to test 
    //  against.
    this.getServer();

    // Initializes simulate property to false.  Can be changed upon starting a
    // test in the testServer method
    this.simulate = false;

    // STATUS VARIABLES
    this.use_websocket_client = false;
    this.websocket_client = null;

  }

  /**
   * Operates on an instance of NDT to begin executing a test.
   * @param - none
   * @return - {string} True if client is using websockets, False if error
   * occurred during finding backend server, or applet container element fall
   * back to Java applet.
   */
  NDTLibjs.prototype.startTest = function() {
    if (NDT.ndtServer) {
      this.checkInstalledPlugins();
      var clientProtocol = this.createBackend();
      this.testNDT().run_test(this.ndtServer);
    } else {
      // in case AJAX call fails or is taking longer
      this.getServer();
      return false;
    }
    return clientProtocol;
  }

  /**
   * Operates on an instance of NDT to determine if the browser supports use of
   * websockets.  If not, will attempt to use Java if available.
   * @param - none
   * @return - none, sets properties within object
   */
  NDTLibjs.prototype.checkInstalledPlugins = function() {
    var hasJava = false;
    var hasWebsockets = false;

    hasJava = true;
    if (typeof deployJava !== 'undefined') {
      if (deployJava.getJREs() == '') {
        hasJava = false;
      }
    }
    hasWebsockets = false;
    try {
      var ndt_js = new NDTjs();
      if (ndt_js.checkBrowserSupport()) {
        hasWebsockets = true;
      }
    } catch(e) {
      hasWebsockets = false;
    }

    if (hasWebsockets) {
      this.use_websocket_client = true;

    }
    else if (hasJava) {
      this.use_websocket_client = false;

    }
  }

  /**
   * Creates the backend for the NDT object.  Tries to create websocket backend
   * if supported, falls back to java applet if websocket not available
   * @param - none
   * @return - {obj} If Java is to be used returns Java applet information, 
   *     otherwise returns false
   */
  NDTLibjs.prototype.createBackend = function() {
    if (this.use_websocket_client) {
      this.websocket_client = new NDTWrapper(this.ndtServer);
      return true;
    }
    else {
      var app = document.createElement('applet');
      app.id = 'NDT';
      app.name = 'NDT';
      app.archive = 'Tcpbw100.jar';
      app.code = 'edu.internet2.ndt.Tcpbw100.class';
      app.width = '600';
      app.height = '10';

      return app;
    }
  }

  /**
   * Determines where to run the tests.  If against the websocket or Java applet
   * @param - none
   * @return - {obj} websocket client or java applet element id
   */
  NDTLibjs.prototype.testNDT = function() {
    if (this.websocket_client) {
      return this.websocket_client;
    }

    return $('#NDT');
  }

  /**
   * AJAX call to determine what server to run tests against.
   * @param - none
   * @return - none, sets object property for server
   */
  NDTLibjs.prototype.getServer = function () {
    var mlabNsService = 'https:' == location.protocol ? 'ndt_ssl' : 'ndt';
    var mlabNsUrl = 'https://mlab-ns.appspot.com/';
    self = this;
    
    var NDTAjax = new XMLHttpRequest();
    NDTAjax.open('GET', mlabNsUrl + mlabNsService + '?format=json', true);
    NDTAjax.onreadystatechange = function () {
      // completed and successful
      if (NDTAjax.readyState == 4 && NDTAjax.status == 200) {
        var resp = JSON.parse(NDTAjax.responseText);
        console.log('Using NDT server: ' + resp.fqdn);
        self.ndtServer = resp.fqdn;
      } 
      // failed
      else if (NDTAjax.readyState == 4 && NDTAjax.status != 200) {
        console.log('mlab-ns lookup failed: ' + NDTAjax.status);
        self.ndtServer = false;
      }
    };
    NDTAjax.send();
  };


  // Monitoring Test Functions

  /**
   * Gets the test status of the current NDT test
   * @param - none
   * @return - {sting} status of current test
   */
  NDTLibjs.prototype.testStatus = function() {
    return this.testNDT().get_status();
  }

  /**
   * Gets error messages of the current NDT test
   * @param - none
   * @return - {sting} error message if occurred
   */
  NDTLibjs.prototype.testError = function() {
    return this.testNDT().get_errmsg();
  }

  /**
   * Gets hostname of server running test
   * @param - none
   * @return - {string} hostname of server
   */
  NDTLibjs.prototype.remoteServer = function() {
    if (this.simulate) return '0.0.0.0';
    return this.testNDT().get_host();
  }

  /**
   * Gets the Upload speed of the current NDT test
   * @param - none
   * @return - {float} speed of uploading to server
   */
  NDTLibjs.prototype.uploadSpeed = function(raw) {
    if (this.simulate) return 0;
    var speed = this.testNDT().getNDTvar('ClientToServerSpeed');
    return raw ? speed : parseFloat(speed);
  }

  /**
   * Gets the Download speed of the current NDT test
   * @param - none
   * @return - {float} speed of downloading from server
   */
  NDTLibjs.prototype.downloadSpeed = function() {
    if (this.simulate) return 0;
    return parseFloat(this.testNDT().getNDTvar('ServerToClientSpeed'));
  }

  /**
   * Gets the Average round trip speed of the current NDT test
   * @param - none
   * @return - {float} speed of round trip
   */
  NDTLibjs.prototype.averageRoundTrip = function() {
    if (this.simulate) return 0;
    return parseFloat(this.testNDT().getNDTvar('avgrtt'));
  }

  /**
   * Gets the Jitter value of the current NDT test
   * @param - none
   * @return - {float} jitter value
   */
  NDTLibjs.prototype.jitter = function() {
    if (this.simulate) return 0;
    return parseFloat(this.testNDT().getNDTvar('Jitter'));
  }

  /**
   * Gets the current top speed of tests
   * @param - none
   * @return - {float} speed limit value during current test
   */
  NDTLibjs.prototype.speedLimit = function() {
    if (this.simulate) return 0;
    return parseFloat(this.testNDT().get_PcBuffSpdLimit());
  }


  // if not already created, instantiate object just in case
  if (typeof(NDT) === 'undefined') {
    window.NDT = new NDTLibjs();
  }
  
})(window);

