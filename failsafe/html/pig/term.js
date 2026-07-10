(function () {
	var URL = {
		STATUS: '/webterm/status',
		DATA: '/webterm/data',
		CMD: '/webterm/cmd',
		ABORT: '/webterm/abort'
	};
	var POLL_INTERVAL = 500;
	var POLL_FAST = 100;
	var FETCH_OPTS = { cache: 'no-cache', headers: { 'Cache-Control': 'no-cache' } };
	var CMD_HEADERS = { 'Content-Type': 'text/plain' };

	var [terminalOutput, terminalInput, sendBtn, clearBtn, connectionStatus, abortBtn] =
		['terminalOutput', 'terminalInput', 'sendBtn', 'clearBtn', 'connectionStatus', 'abortBtn'].map(document.getElementById, document);

	var pollTimer = null;
	var lastSeq = -1;

	function postCommand(body) {
		return fetch(URL.CMD, { method: 'POST', headers: CMD_HEADERS, body: body });
	}

	function restartPoll(delay) {
		clearTimeout(pollTimer);
		pollTimer = setTimeout(fetchStatus, delay);
	}

	function restartPollFast() {
		restartPoll(POLL_FAST);
	}

	async function fetchStatus() {
		var ok = false, hasNewData = false;
		try {
			var response = await fetch(URL.STATUS, FETCH_OPTS);
			if (response.ok) {
				var seq = parseInt(await response.text(), 10);
				if (seq !== lastSeq) {
					await fetchOutput();
					lastSeq = seq;
					hasNewData = true;
				}
				ok = true;
			}
		} catch (error) {}
		connectionStatus.textContent = ok ? '✓' : '✗';
		connectionStatus.style.color = ok ? 'green' : 'red';
		restartPoll(hasNewData ? POLL_FAST : POLL_INTERVAL);
	}

	async function fetchOutput() {
		try {
			var response = await fetch(URL.DATA, FETCH_OPTS);
			var text = await response.text();
			if (text) {
				terminalOutput.textContent += text;
				terminalOutput.scrollTop = terminalOutput.scrollHeight;
				updateButtonStyles();
			}
		} catch (error) {}
	}

	async function sendCommand() {
		var command = terminalInput.value.trim();
		if (!command) return;
		[terminalInput, sendBtn].forEach(function (el) { el.disabled = true; });
		try {
			await postCommand(command);
			terminalInput.value = '';
			restartPollFast();
		} catch (error) { } finally {
			[terminalInput, sendBtn].forEach(function (el) { el.disabled = false; });
			terminalInput.focus();
			updateButtonStyles();
		}
	}

	function clearTerminal() {
		terminalOutput.textContent = '';
		updateButtonStyles();
	}

	var updateEnv = document.getElementById('updateEnv');
	var deleteEnv = document.getElementById('deleteEnv');

	function updateButtonStyles() {
		var hasInput = terminalInput.value.trim().length > 0;
		[sendBtn, updateEnv, deleteEnv].forEach(function (el) {
			el.classList.toggle('active', hasInput);
		});
		clearBtn.classList.toggle('active', terminalOutput.textContent.trim().length > 0);
	}

	function abortCommand() {
		fetch(URL.ABORT, { method: 'POST', headers: CMD_HEADERS });
		restartPollFast();
	}

	terminalInput.addEventListener('input', updateButtonStyles);
	terminalInput.addEventListener('keypress', function (e) { if (e.key === 'Enter') sendCommand(); });
	sendBtn.addEventListener('click', sendCommand);
	clearBtn.addEventListener('click', clearTerminal);
	abortBtn.addEventListener('click', abortCommand);

	document.getElementById('rebootDev').addEventListener('click', function () {
		if (confirm('确认重启？')) { postCommand('reset'); restartPollFast(); }
	});

	var expandBtn = document.getElementById('expandBtn');
	var isFrame = window.self !== window.top;
	expandBtn.href = isFrame ? 'term.html' : 'index.html';
	expandBtn.target = isFrame ? '_blank' : '';
	expandBtn.title = isFrame ? '大窗' : '返回';

	restartPollFast();

	window.TermAPI = {
		postCommand: postCommand,
		restartPollFast: restartPollFast,
		getInput: function () { return terminalInput.value.trim(); }
	};
})();

(function () {
	var api = window.TermAPI;
	var envMenu = document.getElementById('envMenu');
	envMenu.classList.add('show');

	var envToggle = document.getElementById('envToggle');
	envToggle.textContent = '折叠';

	envToggle.addEventListener('click', function () {
		envToggle.textContent = envMenu.classList.toggle('show') ? '折叠' : '展开';
	});

	document.getElementById('queryEnv').addEventListener('click', function () {
		var n = api.getInput();
		api.postCommand(n ? 'printenv ' + n : 'printenv');
		api.restartPollFast();
	});

	document.getElementById('updateEnv').addEventListener('click', function () {
		var n = api.getInput();
		if (!n) { alert('输入变量名'); return; }
		var v = prompt('修改 ' + n + ' 的值:');
		if (v !== null) { api.postCommand('setenv ' + n + ' ' + v + '; saveenv'); api.restartPollFast(); }
	});

	document.getElementById('deleteEnv').addEventListener('click', function () {
		var n = api.getInput();
		if (!n) { alert('输入变量名'); return; }
		if (confirm('确认删除 ' + n + '？')) { api.postCommand('setenv ' + n + '; saveenv'); api.restartPollFast(); }
	});

	document.getElementById('resetEnv').addEventListener('click', function () {
		var n = api.getInput();
		var msg = n ? '将 ' + n + ' 重置为缺省值？' : '重置全部为缺省值？';
		var cmd = n ? 'env default -f ' + n + '; saveenv' : 'env default -a; saveenv';
		if (confirm(msg)) { api.postCommand(cmd); api.restartPollFast(); }
	});
})();

(function() {
	function apply(v) {
		v ? document.documentElement.setAttribute('data-theme', v) : document.documentElement.removeAttribute('data-theme');
	}
	window.addEventListener('storage', function(e) {
		if (e.key === 'theme') apply(e.newValue || '');
	});
	window.addEventListener('message', function(e) {
		if (e.data && e.data.type === 'theme') apply(e.data.theme || '');
	});
})();