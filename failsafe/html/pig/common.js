(function() {
	var t = localStorage.getItem('theme') || '';
	if (t) document.documentElement.setAttribute('data-theme', t);

	function applyTheme(v) {
		v ? document.documentElement.setAttribute('data-theme', v) : document.documentElement.removeAttribute('data-theme');
	}

	window.addEventListener('storage', function(e) {
		if (e.key === 'theme') applyTheme(e.newValue || '');
	});

	window.addEventListener('message', function(e) {
		if (e.data && e.data.type === 'theme') applyTheme(e.data.theme || '');
	});
})();

function formatSize(v) {
	if (v < 1024) return v + ' B';
	if (v < 1048576) return (v / 1024).toFixed(2) + ' KiB';
	if (v < 1073741824) return (v / 1048576).toFixed(2) + ' MiB';
	return (v / 1073741824).toFixed(2) + ' GiB';
}

function formatHex(v) {
	return '0x' + v.toString(16).toUpperCase();
}

function xhrGet(url, cb) {
	var xhr = new XMLHttpRequest();
	xhr.open('GET', url, true);
	xhr.timeout = 10000;
	xhr.onload = function() {
		if (xhr.status !== 200) return;
		var data;
		try { data = JSON.parse(xhr.responseText); } catch(e) { return; }
		cb(data);
	};
	xhr.send();
}

function xhrPost(url, body, cb) {
	var xhr = new XMLHttpRequest();
	xhr.open('POST', url, true);
	xhr.timeout = 10000;
	xhr.onload = function() {
		cb(xhr.status === 200 ? xhr.responseText : null);
	};
	xhr.send(body);
}

function updateTabIndicator(indicatorEl) {
	var active = document.querySelector('.tab-item.active');
	if (!active || !indicatorEl) return;
	var h2 = document.querySelector('.card h2');
	var h2Rect = h2.getBoundingClientRect();
	var activeRect = active.getBoundingClientRect();
	indicatorEl.style.left = (activeRect.left - h2Rect.left) + 'px';
	indicatorEl.style.width = activeRect.width + 'px';
}