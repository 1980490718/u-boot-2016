(function () {
	var nameEl = document.getElementById('env-name');
	var valueEl = document.getElementById('env-value');
	var tips = document.getElementById('tips');

	function getName() { return nameEl.value.trim(); }
	function getValue() { return valueEl.value.trim(); }

	function showResult(data) {
		var text = data.replace(/\n/g, '<br>');
		tips.innerHTML = data.indexOf('ERR:') === 0 ? '<div class="error">' + text + '</div>' : '<div class="success">' + text + '</div>';
	}

	function showError(msg) {
		tips.innerHTML = '<div class="error">' + msg + '</div>';
	}

	function postEnv(body) {
		fetch('/setenv', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
			.then(function (r) { return r.text(); })
			.then(showResult)
			.catch(function (e) { showError(e.message); });
	}

	document.getElementById('query-env').addEventListener('click', function () {
		var name = getName();
		var body = name ? 'var=' + encodeURIComponent(name) : 'var=all';
		fetch('/setenv', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
			.then(function (r) { return r.text(); })
			.then(function (data) {
				if (name) {
					var match = data.match(/\S+=(.*)/);
					if (match) valueEl.value = match[1].replace(/[\x00-\x1f]/g, '');
				}
				showResult(data);
			})
			.catch(function (e) { showError(e.message); });
	});

	document.getElementById('update-env').addEventListener('click', function () {
		var name = getName();
		var value = getValue();
		if (!name || !value) { showError('变量名及值不能为空!'); return; }
		postEnv('var=' + encodeURIComponent(name) + '&val=' + encodeURIComponent(value));
	});

	document.getElementById('delete-env').addEventListener('click', function () {
		var name = getName();
		if (!name) { showError('变量名无效!'); return; }
		postEnv('var=' + encodeURIComponent(name) + '&val=');
	});

	document.getElementById('reset-all').addEventListener('click', function () {
		if (confirm('确认将重置所有环境变量为此U-BOOT内部默认值吗？')) {
			postEnv('var=default');
		}
	});

	document.getElementById('reboot-device').addEventListener('click', function () {
		if (confirm('确认重启设备吗？')) {
			tips.innerHTML = '<div class="loading">重启中...</div>';
			fetch('/reset', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
		}
	});
})();