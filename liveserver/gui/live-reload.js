class HMR {
    constructor() {
        this.ws = null;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
        this.state = {};
        this.connect();
    }

    connect() {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) return;

        try {
            this.ws = new WebSocket('ws://localhost:3001');
            this.ws.onopen = () => {
                console.log('HMR Connected');
                this.showStatus('Connected', 'success');
                this.reconnectAttempts = 0;
            };

            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    this.handleUpdate(data);
                } catch (e) {
                    console.error('HMR message parse error:', e);
                }
            };

            this.ws.onclose = () => {
                console.log('HMR disconnected');
                this.showStatus('Disconnected', 'error');
                this.reconnect();
            };
        } catch (error) {
            console.error('WebSocket connection failed:', error);
            setTimeout(() => this.connect(), 1000);
        }
    }

    handleUpdate(data) {
        switch (data.type) {
            case 'css':
                this.updateCSS(data.filename, data.content);
                break;
            case 'js':
                this.updateJS(data.filename, data.content);
                break;
            case 'html':
                this.updateHTML(data.filename, data.content);
                break;
            case 'reload':
                this.fullReload();
                break;
        }
    }

    updateCSS(filename, content) {
        this.preserveState();

        let link = document.querySelector(`link[href*='${filename}']`);
        if (link) {
            const newLink = link.cloneNode();
            newLink.href = filename + '?t=' + Date.now();
            link.parentNode.insertBefore(newLink, link.nextSibling);
            setTimeout(() => link.remove(), 100);
        } else {
            // Update inline styles
            const styles = document.querySelectorAll('style');
            styles.forEach(style => {
                if (style.textContent.includes(filename.replace('.css', ''))) {
                    style.textContent = content;
                }
            });
        }

        this.showStatus(`CSS Updated: ${filename}`, 'success');
        this.restoreState();
    }

    updateJS(filename, content) {
        this.preserveState();

        // For now, reload for JS changes (can be improved)
        console.log(`⚡ JS file changed: ${filename}`);
        this.showStatus(`⚡ JS Updated: ${filename}`, 'warning');
        setTimeout(() => this.fullReload(), 500);
    }

    updateHTML(filename, content) {
        this.preserveState();
        this.showStatus(`HTML Updated: ${filename}`, 'warning');
        setTimeout(() => this.fullReload(), 300);
    }

    fullReload() {
        console.log('Full page reload');
        location.reload();
    }

    preserveState() {
        this.state.scroll = window.scrollY;
        this.state.inputs = {};

        document.querySelectorAll('input, textarea, select').forEach(el => {
            if (el.id || el.name) {
                this.state.inputs[el.id || el.name] = el.value;
            }
        });
    }

    restoreState() {
        if (this.state.scroll !== undefined) {
            window.scrollTo(0, this.state.scroll);
        }

        Object.keys(this.state.inputs || {}).forEach(key => {
            const el = document.getElementById(key) || document.querySelector(`[name='${key}']`);
            if (el) el.value = this.state.inputs[key];
        });
    }

    showStatus(message, type) {
        let status = document.getElementById('hmr-status');
        if (!status) {
            status = document.createElement('div');
            status.id = 'hmr-status';
            status.style.cssText = `
                position: fixed; top: 20px; right: 20px; z-index: 10000;
                padding: 8px 16px; border-radius: 4px; font-size: 12px;
                font-family: monospace; font-weight: bold;
                transition: all 0.3s ease; transform: translateX(100%);
            `;
            document.body.appendChild(status);
        }

        const colors = {
            success: 'background: #10b981; color: white;',
            warning: 'background: #f59e0b; color: white;',
            error: 'background: #ef4444; color: white;'
        };

        status.style.cssText += colors[type] || colors.success;
        status.textContent = message;
        status.style.transform = 'translateX(0)';

        setTimeout(() => {
            status.style.transform = 'translateX(100%)';
        }, 3000);
    }

    reconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            console.log('Max reconnection attempts reached');
            return;
        }

        this.reconnectAttempts++;
        setTimeout(() => this.connect(), 1000 * this.reconnectAttempts);
    }
}

// Initialize
if (typeof window !== 'undefined') {
    window.HMR = new HMR();

    // Fallback polling for servers without WebSocket
    setInterval(async () => {
        try {
            const response = await fetch('/reload');
            const data = await response.json();
            if (data.reload && (!window.HMR.ws || window.HMR.ws.readyState !== WebSocket.OPEN)) {
                console.log('Fallback reload detected');
                window.HMR.showStatus('File changed (fallback)', 'warning');
                setTimeout(() => location.reload(), 300);
            }
        } catch (e) {
            // Ignore polling errors
        }
    }, 1000);
}