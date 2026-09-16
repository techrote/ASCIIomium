(() => {
  const params = new URLSearchParams(location.search);
  const frozen = params.get('freeze') === '1';
  document.documentElement.dataset.freeze = frozen ? 'true' : 'false';
  document.documentElement.dataset.fixtureReady = 'true';
  window.ASCIIOMIUM_FIXTURE_VERSION = 'web-fixtures-v1';
  window.ASCIIOMIUM_FIXTURE_FROZEN = frozen;
})();
