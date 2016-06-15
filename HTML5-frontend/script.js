/**
 * @fileoverview Reflects the functionality that is used on the NDT frontend on
 * the mlab website.
 *
 * Dependencies: ndt-client-wrapper.js ndt-wrapper.js, ndt-browser-client.js,
 * gauge.min.js, jQuery
 */

/*jslint bitwise: true, browser: true, indent: 2, nomen: true */
/*global $, jQuery, Gauge, NDT, simulate*/

'use strict';

var simulate = false,
  allowDebug;

// CONSTANTS

// Testing phases

var PHASE_LOADING   = 0;
var PHASE_WELCOME   = 1;
var PHASE_PREPARING = 2;
var PHASE_UPLOAD    = 3;
var PHASE_DOWNLOAD  = 4;
var PHASE_RESULTS   = 5;


// STATUS VARIABLES
var use_websocket_client = false;
var websocket_client = null;
var currentPhase = PHASE_LOADING;
var currentPage = 'welcome';
var transitionSpeed = 400;

// Gauges used for showing download/upload speed
var downloadGauge, uploadGauge;
var gaugeUpdateInterval;
var gaugeMaxValue = 1000;

// TESTING JAVA/WEBSOCKET CLIENT

/**
 * Parses, formats, and displays final diagnostic information for NDT test
 */
function testDiagnosis() {
  var div = document.createElement('div'),
    diagnosisArray = NDT.testNDT().get_diagnosis().split('\n'),
    txt = '',
    table,
    isTable = false;

  if (simulate) {
    div.innerHTML = 'Test diagnosis';
    return div;
  }

  diagnosisArray.forEach(
    function addRow(value) {
      if (isTable) {
        var rowArray = value.split(':'),
          row;
        if (rowArray.length > 1) {
          row = table.insertRow(-1);
          rowArray.forEach(
            function addCell(cellValue, idx) {
              var cell = row.insertCell(idx);
              cell.innerHTML = cellValue;
            }
          );
        } else {
          isTable = false;
          txt = txt + value;
        }
      } else {
        if (value.indexOf('=== Results sent by the server ===') !== -1) {
          table = document.createElement('table');
          isTable = true;
        } else {
          txt = txt + value + '\n';
        }
      }
    }
  );
  txt = txt + '=== Results sent by the server ===';
  div.innerHTML = txt;
  if (isTable) {
    div.appendChild(table);
  }

  return div;
}

function printPacketLoss() {
  var packetLoss = parseFloat(NDT.testNDT().getNDTvar('loss'));
  packetLoss = (packetLoss * 100).toFixed(2);
  return packetLoss;
}

function printNumberValue(value) {
  return isNaN(value) ? '-' : value;
}

function printJitter(boldValue) {
  var retStr = '',
    jitterValue = NDT.jitter();
  if (jitterValue >= 1000) {
    retStr += (boldValue ? '<b>' : '') + printNumberValue(jitterValue / 1000) + (boldValue ? '</b>' : '') + ' sec';
  } else {
    retStr += (boldValue ? '<b>' : '') + printNumberValue(jitterValue) + (boldValue ? '</b>' : '') + ' msec';
  }
  return retStr;
}

function getSpeedUnit(speedInKB) {
  var unit = ['kb/s', 'Mb/s', 'Gb/s', 'Tb/s', 'Pb/s', 'Eb/s'],
    e = Math.floor(Math.log(speedInKB * 1000) / Math.log(1000));
  return unit[e];
}

function getJustfiedSpeed(speedInKB) {
  var e = Math.floor(Math.log(speedInKB) / Math.log(1000));
  return (speedInKB / Math.pow(1000, e)).toFixed(2);
}

function printDownloadSpeed() {
  var downloadSpeedVal = NDT.downloadSpeed();
  $('#download-speed').html(getJustfiedSpeed(downloadSpeedVal));
  $('#download-speed-units').html(getSpeedUnit(downloadSpeedVal));
}

function printUploadSpeed() {
  var uploadSpeedVal = NDT.uploadSpeed(false);
  $('#upload-speed').html(getJustfiedSpeed(uploadSpeedVal));
  $('#upload-speed-units').html(getSpeedUnit(uploadSpeedVal));
}

function readNDTvar(variable) {
  var ret = NDT.testNDT().getNDTvar(variable);
  return !ret ? '-' : ret;
}

/**
 * Parses, formats, and displays final summary of diagnostic information for
 * NDT test
 */
function testDetails() {
  var d = '',
    errorMsg;

  if (simulate) {
    return 'Test details';
  }

  errorMsg = NDT.testError();

  if (errorMsg.match(/failed/)) {
    d += 'Error occured while performing test: <br>'.bold();
    if (errorMsg.match(/#2048/)) {
      d += 'Security error. This error may be caused by firewall issues, make sure that port 843 is available on the NDT server, and that you can access it.'.bold().fontcolor('red') + '<br><br>';
    } else {
      d += errorMsg.bold().fontcolor('red') + '<br><br>';
    }
  }

  d += 'Your system: ' + readNDTvar('OperatingSystem').bold() + '<br>';
  d += 'Plugin version: ' + (readNDTvar('PluginVersion') + ' (' + readNDTvar('OsArchitecture') + ')<br>').bold();

  d += '<br>';

  d += 'TCP receive window: ' + readNDTvar('CurRwinRcvd').bold() + ' current, ' + readNDTvar('MaxRwinRcvd').bold() + ' maximum<br>';
  d += '<b>' + printNumberValue(printPacketLoss()) + '</b> % of packets lost during test<br>';
  d += 'Round trip time: ' + readNDTvar('MinRTT').bold() + ' msec (minimum), ' + readNDTvar('MaxRTT').bold() + ' msec (maximum), <b>' + printNumberValue(Math.round(NDT.averageRoundTrip())) + '</b> msec (average)<br>';
  d += 'Jitter: ' + printNumberValue(printJitter(true)) + '<br>';
  d += readNDTvar('waitsec').bold() + ' seconds spend waiting following a timeout<br>';
  d += 'TCP time-out counter: ' + readNDTvar('CurRTO').bold() + '<br>';
  d += readNDTvar('SACKsRcvd').bold() + ' selective acknowledgement packets received<br>';

  d += '<br>';

  if (readNDTvar('mismatch') === 'yes') {
    d += 'A duplex mismatch condition was detected.<br>'.fontcolor('red').bold();
  } else {
    d += 'No duplex mismatch condition was detected.<br>'.fontcolor('green');
  }

  if (readNDTvar('bad_cable') === 'yes') {
    d += 'The test detected a cable fault.<br>'.fontcolor('red').bold();
  } else {
    d += 'The test did not detect a cable fault.<br>'.fontcolor('green');
  }

  if (readNDTvar('congestion') === 'yes') {
    d += 'Network congestion may be limiting the connection.<br>'.fontcolor('red').bold();
  } else {
    d += 'No network congestion was detected.<br>'.fontcolor('green');
  }

  d += '<br>';

  d += printNumberValue(readNDTvar('cwndtime')).bold() + ' % of the time was not spent in a receiver limited or sender limited state.<br>';
  d += printNumberValue(readNDTvar('rwintime')).bold() + ' % of the time the connection is limited by the client machine\'s receive buffer.<br>';
  d += 'Optimal receive buffer: ' + printNumberValue(readNDTvar('optimalRcvrBuffer')).bold() + ' bytes<br>';
  d += 'Bottleneck link: ' + readNDTvar('accessTech').bold() + '<br>';
  d += readNDTvar('DupAcksIn').bold() + ' duplicate ACKs set<br>';

  return d;
}

// GAUGE

/**
 * Sets up gauges used in client to display upload/download speeds as results
 * are coming in.
 */
function initializeGauges() {
  var gaugeValues = [],
    i;

  for (i = 0; i <= 10; i += 1) {
    gaugeValues.push(0.1 * gaugeMaxValue * i);
  }
  uploadGauge = new Gauge({
    renderTo    : 'uploadGauge',
    width       : 270,
    height      : 270,
    units       : 'Mb/s',
    title       : 'Upload',
    minValue    : 0,
    maxValue    : gaugeMaxValue,
    majorTicks  : gaugeValues,
    highlights  : [{ from: 0, to: gaugeMaxValue, color: 'rgb(0, 255, 0)' }]
  });

  gaugeValues = [];
  for (i = 0; i <= 10; i += 1) {
    gaugeValues.push(0.1 * gaugeMaxValue * i);
  }
  downloadGauge = new Gauge({
    renderTo    : 'downloadGauge',
    width       : 270,
    height      : 270,
    units       : 'Mb/s',
    title       : 'Download',
    minValue    : 0,
    maxValue    : gaugeMaxValue,
    majorTicks  : gaugeValues,
    highlights  : [{ from: 0, to: gaugeMaxValue, color: 'rgb(0, 255, 0)' }]
  });
}

function resetGauges() {
  var gaugeConfig = [];

  gaugeConfig.push({
    from: 0,
    to: gaugeMaxValue,
    color: 'rgb(0, 255, 0)'
  });

  uploadGauge.updateConfig({
    highlights: gaugeConfig
  });
  uploadGauge.setValue(0);

  downloadGauge.updateConfig({
    highlights: gaugeConfig
  });
  downloadGauge.setValue(0);
}

function updateGaugeValue() {
  var downloadSpeedVal = NDT.downloadSpeed(),
    uploadSpeedVal = NDT.uploadSpeed(false);

  if (currentPhase === PHASE_UPLOAD) {
    uploadGauge.updateConfig({
      units: getSpeedUnit(uploadSpeedVal)
    });
    uploadGauge.setValue(getJustfiedSpeed(uploadSpeedVal));
  } else if (currentPhase === PHASE_DOWNLOAD) {
    downloadGauge.updateConfig({
      units: getSpeedUnit(downloadSpeedVal)
    });
    downloadGauge.setValue(getJustfiedSpeed(downloadSpeedVal));
  } else {
    clearInterval(gaugeUpdateInterval);
  }
}

// UTILITIES

function debug(message) {
  if (allowDebug !== 'undefined') {
    if (allowDebug && window.console) {
      console.debug(message);
    }
  }
}

function isPluginLoaded() {
  try {
    NDT.testStatus();
    return true;
  } catch (e) {
    return false;
  }
}

/**
 * Attempts to determine the absolute path of a script, minus the name of the
 * script itself.
 * @return {string} path of script
 */
function getScriptPath() {
  var scripts = document.getElementById('ndt-wrapper'),
    fileRegex = new RegExp('\/ndt-wrapper.js$'),
    path = '';

  if (scripts) {
    path = scripts.src.replace(fileRegex, '');
  }
  return path.substring(location.origin.length);
}

// PAGES
/**
 * Shows or hides certain container elements on the front end depending on the
 * phase the test is currently in.
 * @param {string} id of page to be shown
 * @param {string} optional, callback method
 */
function showPage(page, callback) {
  debug('Show page: ' + page);
  if (page === currentPage) {
    if (callback !== undefined) {
      callback();
    }
    return true;
  }
  if (currentPage !== undefined) {
    $('#' + currentPage).fadeOut(transitionSpeed, function () {
      $('#' + page).fadeIn(transitionSpeed, callback);
    });
  } else {
    debug('No current page');
    $('#' + page).fadeIn(transitionSpeed, callback);
  }
  currentPage = page;
}

// RESULTS

/**
 * Shows or hides certain tabs on results screen
 * @param {string} id of tab
 */
function showResultsPage(page) {
  var pages = ['summary', 'details', 'advanced'],
    i,
    len;
  debug('Results: show ' + page);
  for (i = 0, len = pages.length; i < len; i + 1) {
    $('#results')[(page === pages[i]) ? 'addClass' : 'removeClass'](pages[i]);
  }
}

function showResultsSummary() {
  showResultsPage('summary');
}

function showResultsDetails() {
  showResultsPage('details');
}

function showResultsAdvanced() {
  showResultsPage('advanced');
}

// PHASES

function setPhase(phase) {
  switch (phase) {

  case PHASE_WELCOME:
    debug('WELCOME');
    showPage('welcome');
    break;

  case PHASE_PREPARING:
    uploadGauge.setValue(0);
    downloadGauge.setValue(0);
    debug('PREPARING TEST');

    $('#loading').show();
    $('#upload').hide();
    $('#download').hide();

    showPage('test', resetGauges);
    break;

  case PHASE_UPLOAD:
    var pcBuffSpdLimit, rtt, gaugeConfig = [];
    debug('UPLOAD TEST');

    pcBuffSpdLimit = NDT.speedLimit();
    rtt = NDT.averageRoundTrip();

    if (isNaN(rtt)) {
      $('#rttValue').html('n/a');
    } else {
      $('rttValue').html(printNumberValue(Math.round(rtt)) + ' ms');
    }

    if (!isNaN(pcBuffSpdLimit)) {
      if (pcBuffSpdLimit > gaugeMaxValue) {
        pcBuffSpdLimit = gaugeMaxValue;
      }
      gaugeConfig.push({
        from: 0,
        to: pcBuffSpdLimit,
        color: 'rgb(0, 255, 0)'
      });

      gaugeConfig.push({
        from: pcBuffSpdLimit,
        to: gaugeMaxValue,
        color: 'rgb(255, 0, 0)'
      });

      uploadGauge.updateConfig({
        highlights: gaugeConfig
      });

      downloadGauge.updateConfig({
        highlights: gaugeConfig
      });
    }

    $('#loading').hide();
    $('#upload').show();

    gaugeUpdateInterval = setInterval(function () {
      updateGaugeValue();
    }, 1000);

    $('#test .remote.location .address').get(0).innerHTML = NDT.remoteServer();
    break;

  case PHASE_DOWNLOAD:
    debug('DOWNLOAD TEST');

    $('#upload').hide();
    $('#download').show();
    break;

  case PHASE_RESULTS:
    debug('SHOW RESULTS');
    debug('Testing complete');

    printDownloadSpeed();
    printUploadSpeed();
    $('#latency').html(printNumberValue(Math.round(NDT.averageRoundTrip())));
    $('#jitter').html(printJitter(false));
    $('#test-details').html(testDetails());
    $('#test-advanced').append(testDiagnosis());
    $('#javaButton').attr('disabled', false);

    showPage('results');
    break;

  default:
    return false;
  }
  currentPhase = phase;
}

// PRIMARY METHODS

/**
 * Function used for testing purposes to simulate an NDT test without actually
 * interacting with the backend data.
 */
function simulateTest() {
  setPhase(PHASE_RESULTS);
  setPhase(PHASE_PREPARING);
  setTimeout(function () { setPhase(PHASE_UPLOAD); }, 2000);
  setTimeout(function () { setPhase(PHASE_DOWNLOAD); }, 4000);
  setTimeout(function () { setPhase(PHASE_RESULTS); }, 6000);
  return;
}

/**
 * Contains functionality that will handle errors as well as update the front
 * end with speed information and change of status phases.
 * @return {boolean} - optional, if test is completed or errored out.
 */
function monitorTest() {
  var message = NDT.testError(),
    currentStatus = NDT.testStatus();

  if (message.match(/not run/) && currentPhase !== PHASE_LOADING) {
    setPhase(PHASE_WELCOME);
    return false;
  }
  if (message.match(/completed/) && currentPhase < PHASE_RESULTS) {
    setPhase(PHASE_RESULTS);
    return true;
  }
  if (message.match(/failed/) && currentPhase < PHASE_RESULTS) {
    setPhase(PHASE_RESULTS);
    return false;
  }
  if (currentStatus.match(/Outbound/) && currentPhase < PHASE_UPLOAD) {
    setPhase(PHASE_UPLOAD);
  }
  if (currentStatus.match(/Inbound/) && currentPhase < PHASE_DOWNLOAD) {
    setPhase(PHASE_DOWNLOAD);
  }

  if (!currentStatus.match(/Middleboxes/) && !currentStatus.match(/notStarted/)
        && !NDT.remoteServer().match(/ndt/) && currentPhase === PHASE_PREPARING) {
    debug('Remote server is ' + NDT.remoteServer());
    setPhase(PHASE_UPLOAD);
  }

  if (NDT.remoteServer() !== 'unknown' && currentPhase < PHASE_PREPARING) {
    setPhase(PHASE_PREPARING);
  }

  setTimeout(monitorTest, 1000);
}

/**
 * Function that starts the NDT test executing.  Depending on what is returned
 * from NDT library, accounts for using a websocket or Java Applet type of test
 * @param {event} - event that triggered function
 */
function startTest(evt) {
  var clientProtocol;

  //stop button click behavior and start NDT test
  evt.stopPropagation();
  evt.preventDefault();
  if (simulate) {
    return simulateTest();
  }

  $('#backendContainer').empty();
  clientProtocol = NDT.startTest();
  if (clientProtocol instanceof Error) {
    $('#warning-plugin').show();
    return false;
  }
  if (typeof clientProtocol === "object") {
    $('#backendContainer').append(clientProtocol);
  }
  $('#javaButton').attr('disabled', true);
  $('#websocketButton').attr('disabled', true);
  showPage('test', resetGauges);
  $('#rttValue').html('');
  currentPhase = PHASE_WELCOME;
  monitorTest();
}

/**
 * Sets up the front end to run NDT client by initializing the spped gauges,
 * setting the initial phase to "welcome", and adding event listeners for
 * result page tabs/buttons
 */
function initializeTest() {
  // Initialize gauges
  initializeGauges();

  // Initialize start button
  $('.start.button').click(startTest);

  // Results view selector
  $('#results .view-selector .summary').click(showResultsSummary);
  $('#results .view-selector .details').click(showResultsDetails);
  $('#results .view-selector .advanced').click(showResultsAdvanced);

  $('body').removeClass('initializing');
  $('body').addClass('ready');
  setPhase(PHASE_WELCOME);
}

$(function () {
  jQuery.fx.interval = 50;
  if (simulate) {
    setTimeout(initializeTest, 1000);
    return;
  }
  initializeTest();
});
