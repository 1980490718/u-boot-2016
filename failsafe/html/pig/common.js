(function() {
	document.documentElement.classList.add('no-transition');
	var t = localStorage.getItem('theme') || '';
	if (t) document.documentElement.setAttribute('data-theme', t);
	requestAnimationFrame(function() {
		requestAnimationFrame(function() {
			document.documentElement.classList.remove('no-transition');
		});
	});

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

var ST = ['上传', '验证', '刷写', '重启', '访问'];
function showStep(cur, desc, from) {
	var h = '';
	for (var i = from || 0; i < 5; i++)
		h += (i < cur ? ST[i] + 'ok ' : i === cur ? ST[i] + '<svg class="icon icon-spin" viewBox="0 0 24 24"><use href="icons.svg?v=f#icon-refresh"/></svg> ' : '- ' + ST[i] + ' ');
	var el = document.querySelector('.card');
	if (!el) { el = document.createElement('div'); el.className = 'card'; (document.querySelector('main') || document.body).appendChild(el); }
	el.innerHTML = '<h2>' + ST[cur] + '</h2><p>' + h + '</p><p>' + desc + '</p>';
}
function pingDevice(from) {
	var p = window.location.origin.match(/^(https?:\/\/)/)[1], q = p + '192.168.';
	var ips = [window.location.origin, q+'1.1', q+'0.1', q+'10.1', q+'20.1', q+'30.1', q+'66.1', q+'68.1', q+'88.1', p+'6.6.6.6', p+'6.7.8.9'];
	var ph = [[20,20,'设备重启中...'], [40,100,'系统加载中...'], [20,10,'尝试连接中...']];
	var pi = 0;
	function run() {
		if (pi >= ph.length) return showStep(4, '请手动访问设备ip地址', from);
		var c = ph[pi++];
		showStep(3, c[2], from);
		setTimeout(function() {
			var r = 0;
			function go() {
				if (r++ >= c[1]) return run();
				var hit = 0, fail = 0;
				ips.forEach(function(ip) {
					fetch(ip, {mode:'no-cors', cache:'no-cache'}).then(function() {
						!hit++ && (window.top.location.href = ip);
					}).catch(function() {
						++fail >= ips.length && setTimeout(go, 500);
					});
				});
			}
			go();
		}, c[0] * 1000);
	}
	run();
}