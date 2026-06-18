(function () {
	var terminalOutput = document.getElementById('terminalOutput');
	var terminalInput = document.getElementById('terminalInput');
	var sendBtn = document.getElementById('sendBtn');
	var clearBtn = document.getElementById('clearBtn');
	var connectionStatus = document.getElementById('connectionStatus');

	var POLL_INTERVAL = 500;
	var FETCH_OPTS = { cache: 'no-cache', headers: { 'Cache-Control': 'no-cache' } };
	var pollTimer = null;
	var lastSeq = -1;

	async function fetchStatus() {
	try {
		const response = await fetch('/webterm/status', FETCH_OPTS);
		if (response.ok) {
			const seq = parseInt(await response.text(), 10);
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
	if (pollTimer) clearTimeout(pollTimer);
	pollTimer = setTimeout(fetchStatus, POLL_INTERVAL);
}

	async function fetchOutput() {
	try {
		const response = await fetch('/webterm/data', FETCH_OPTS);
		const text = await response.text();
		if (text) {
			terminalOutput.textContent += text;
			terminalOutput.scrollTop = terminalOutput.scrollHeight;
			updateButtonStyles();
		}
	} catch (error) { }
}

	async function sendCommand() {
	const command = terminalInput.value.trim();
	if (!command) return;

	[terminalInput, sendBtn].forEach(el => el.disabled = true);

	try {
		await fetch('/webterm/cmd', {
			method: 'POST',
			headers: { 'Content-Type': 'text/plain' },
			body: command
		});
		terminalInput.value = '';
		clearTimeout(pollTimer);
		pollTimer = setTimeout(fetchStatus, 100);
	} catch (error) { } finally {
		[terminalInput, sendBtn].forEach(el => el.disabled = false);
		terminalInput.focus();
		updateButtonStyles();
	}
}

	function clearTerminal() {
	terminalOutput.textContent = '';
	updateButtonStyles();
}

	function updateButtonStyles() {
	sendBtn.classList.toggle('active', terminalInput.value.trim().length > 0);
	clearBtn.classList.toggle('active', terminalOutput.textContent.trim().length > 0);
}

	Object.entries({
			input: updateButtonStyles,
			keypress: function (e) { if (e.key === 'Enter') sendCommand(); }
		}).forEach(function (entry) { terminalInput.addEventListener(entry[0], entry[1]); });

	[sendBtn, clearBtn].forEach(function (btn, i) { btn.addEventListener('click', [sendCommand, clearTerminal][i]); });

	if (pollTimer) clearTimeout(pollTimer);
	pollTimer = setTimeout(fetchStatus, POLL_INTERVAL);

	var expandBtn = document.getElementById('expandBtn');
	if (window.self !== window.top) {
		expandBtn.href = 'term.html';
		expandBtn.target = '_blank';
		expandBtn.title = '大窗';
	} else {
		expandBtn.href = 'index.html';
		expandBtn.title = '返回';
	}
})();