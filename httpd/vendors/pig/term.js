(function () {
	var URL = {
		STATUS: '/webterm/status',
		DATA: '/webterm/data',
		CMD: '/webterm/cmd'
	};
	var POLL_INTERVAL = 500;
	var POLL_FAST = 100;
	var FETCH_OPTS = { cache: 'no-cache', headers: { 'Cache-Control': 'no-cache' } };
	var CMD_HEADERS = { 'Content-Type': 'text/plain' };

	var terminalOutput = document.getElementById('terminalOutput');
	var terminalInput = document.getElementById('terminalInput');
	var sendBtn = document.getElementById('sendBtn');
	var clearBtn = document.getElementById('clearBtn');
	var connectionStatus = document.getElementById('connectionStatus');

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
		try {
			var response = await fetch(URL.STATUS, FETCH_OPTS);
			if (response.ok) {
				var seq = parseInt(await response.text(), 10);
				if (seq !== lastSeq) {
					lastSeq = seq;
					await fetchOutput();
				}
				connectionStatus.textContent = '✓';
				connectionStatus.style.color = 'green';
			}
		} catch (error) {
			connectionStatus.textContent = '✗';
			connectionStatus.style.color = 'red';
		}
		restartPoll(POLL_INTERVAL);
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
		} catch (error) { }
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

	function updateButtonStyles() {
		var hasInput = terminalInput.value.trim().length > 0;
		sendBtn.classList.toggle('active', hasInput);
		clearBtn.classList.toggle('active', terminalOutput.textContent.trim().length > 0);
		document.getElementById('updateEnv').classList.toggle('active', hasInput);
		document.getElementById('deleteEnv').classList.toggle('active', hasInput);
	}

	terminalInput.addEventListener('input', updateButtonStyles);
	terminalInput.addEventListener('keypress', function (e) { if (e.key === 'Enter') sendCommand(); });
	sendBtn.addEventListener('click', sendCommand);
	clearBtn.addEventListener('click', clearTerminal);

	document.getElementById('rebootDev').addEventListener('click', function () {
		if (confirm('确认重启？')) { postCommand('reset'); restartPollFast(); }
	});

	var expandBtn = document.getElementById('expandBtn');
	if (window.self !== window.top) {
		expandBtn.href = 'term.html';
		expandBtn.target = '_blank';
		expandBtn.title = '大窗';
	} else {
		expandBtn.href = 'index.html';
		expandBtn.title = '返回';
	}

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
		envMenu.classList.toggle('show');
		envToggle.textContent = envMenu.classList.contains('show') ? '折叠' : '展开';
	});

	document.getElementById('queryEnv').addEventListener('click', function () {
		var n = api.getInput();
		api.postCommand(n ? 'printenv ' + n : 'printenv');
		api.restartPollFast();
	});

	document.getElementById('updateEnv').addEventListener('click', function () {
		var n = api.getInput();
		if (!n) { alert('请先输入变量名'); return; }
		var v = prompt('修改变量 ' + n + ' 的值:');
		if (v !== null) { api.postCommand('setenv ' + n + ' ' + v + '; saveenv'); api.restartPollFast(); }
	});

	document.getElementById('deleteEnv').addEventListener('click', function () {
		var n = api.getInput();
		if (!n) { alert('请先输入变量名'); return; }
		if (confirm('确认删除变量 ' + n + '？')) { api.postCommand('setenv ' + n + '; saveenv'); api.restartPollFast(); }
	});

	document.getElementById('resetEnv').addEventListener('click', function () {
		var n = api.getInput();
		if (n) {
			if (confirm('确认将变量 ' + n + ' 重置为内置默认值？')) { api.postCommand('env default -f ' + n + '; saveenv'); api.restartPollFast(); }
		} else {
			if (confirm('确认重置所有环境变量为内置默认值？')) { api.postCommand('env default -a; saveenv'); api.restartPollFast(); }
		}
	});
})();