import { getContext, setContext } from 'svelte';
import {
	settingsBridge,
	type ContentBlockingState,
	type DomainImportFormat,
	type DomainImportPreview
} from '#lib/bridge.ts';
import { t } from '#lib/i18n.ts';

export class SettingsModel {
	state = $state<ContentBlockingState>();
	loadError = $state(false);
	updating = $state(false);
	changingException = $state(false);
	changingDomains = $state(false);
	domainFeedback = $state('');
	domainFeedbackError = $state(false);
	feedback = $state('');
	feedbackError = $state(false);
	private refreshEpoch = 0;
	private disposed = false;

	start(): () => void {
		const unsubscribe = settingsBridge.subscribe(() => {
			void this.refresh();
		});
		void this.refresh();
		return () => {
			this.disposed = true;
			++this.refreshEpoch;
			unsubscribe();
		};
	}

	async refresh(): Promise<void> {
		const epoch = ++this.refreshEpoch;
		try {
			const next = await settingsBridge.getState();
			if (this.disposed || epoch !== this.refreshEpoch) return;
			this.state = next;
			this.loadError = false;
		} catch {
			if (!this.disposed && epoch === this.refreshEpoch) this.loadError = true;
		}
	}

	private message(key: string, error = false): void {
		this.feedback = t(key);
		this.feedbackError = error;
	}

	async changeException(input: string, enabled: boolean): Promise<boolean> {
		if (this.changingException || !this.state?.enabled) return false;
		this.changingException = true;
		try {
			const next = await settingsBridge.setException(input.trim(), enabled);
			if (this.disposed) return false;
			++this.refreshEpoch;
			this.state = next;
			this.message(enabled ? 'exceptionRemoved' : 'exceptionAdded');
			return true;
		} catch (error) {
			if (!this.disposed)
				this.message(error === 'invalid-site' ? 'invalidSite' : 'changeFailed', true);
			return false;
		} finally {
			if (!this.disposed) this.changingException = false;
		}
	}

	async checkLists(): Promise<void> {
		if (this.updating || this.state?.updating || !this.state?.updatesAvailable) return;
		this.updating = true;
		this.message('checkingFeedback');
		try {
			const next = await settingsBridge.checkLists();
			if (this.disposed) return;
			++this.refreshEpoch;
			this.state = next;
			this.message(
				next.updateSucceeded ? (next.pendingRestart ? 'updateReady' : 'upToDate') : 'updateFailed',
				!next.updateSucceeded
			);
		} catch (error) {
			if (!this.disposed)
				this.message(error === 'updates-unavailable' ? 'updatesUnavailable' : 'updateFailed', true);
		} finally {
			if (!this.disposed) this.updating = false;
		}
	}

	private domainError(error: unknown): void {
		const keys: Record<string, string> = {
			'invalid-domain': 'invalidDomain',
			'duplicate-domain': 'domainAlreadyAdded',
			'file-too-large': 'domainImportTooLarge',
			'too-many-domains': 'domainImportLimit',
			'empty-file': 'domainImportEmpty',
			'invalid-file': 'domainImportInvalidFile',
			'invalid-csv': 'domainImportInvalidFile',
			'invalid-csv-header': 'domainImportInvalidFile'
		};
		if (!this.disposed) {
			this.domainFeedback = t(keys[String(error)] ?? 'changeFailed');
			this.domainFeedbackError = true;
		}
	}

	private async editDomain(
		request: () => Promise<ContentBlockingState>,
		message: string
	): Promise<boolean> {
		if (this.changingDomains || !this.state?.enabled) return false;
		this.changingDomains = true;
		try {
			const next = await request();
			if (this.disposed) return false;
			++this.refreshEpoch;
			this.state = next;
			this.domainFeedback = t(message);
			this.domainFeedbackError = false;
			return true;
		} catch (error) {
			this.domainError(error);
			return false;
		} finally {
			if (!this.disposed) this.changingDomains = false;
		}
	}

	addDomain(domain: string, includeSubdomains: boolean): Promise<boolean> {
		return this.editDomain(
			() => settingsBridge.addDomain(domain.trim(), includeSubdomains),
			'domainSaved'
		);
	}

	changeDomainScope(domain: string, includeSubdomains: boolean): Promise<boolean> {
		return this.editDomain(
			() => settingsBridge.setDomain(domain.trim(), includeSubdomains),
			'domainSaved'
		);
	}

	removeDomain(domain: string): Promise<boolean> {
		return this.editDomain(() => settingsBridge.removeDomain(domain), 'domainRemoved');
	}

	async previewDomains(
		format: DomainImportFormat,
		text: string
	): Promise<DomainImportPreview | undefined> {
		if (!this.state?.enabled) return;
		this.domainFeedback = '';
		try {
			const preview = await settingsBridge.previewDomains(format, text);
			if (!this.disposed) return preview;
		} catch (error) {
			this.domainError(error);
		}
	}

	async importDomains(format: DomainImportFormat, text: string): Promise<boolean> {
		if (this.changingDomains || !this.state?.enabled) return false;
		this.changingDomains = true;
		try {
			const added = await settingsBridge.importDomains(format, text);
			await this.refresh();
			if (this.disposed) return false;
			this.domainFeedback = t('domainImportAdded', String(added));
			this.domainFeedbackError = false;
			return true;
		} catch (error) {
			this.domainError(error);
			return false;
		} finally {
			if (!this.disposed) this.changingDomains = false;
		}
	}
}

const contextKey = Symbol('settings');
export function provideSettings(): SettingsModel {
	return setContext(contextKey, new SettingsModel());
}
export function useSettings(): SettingsModel {
	return getContext(contextKey);
}
