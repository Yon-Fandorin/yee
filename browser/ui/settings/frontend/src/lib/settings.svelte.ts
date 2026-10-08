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
	changingSubscriptions = $state(false);
	subscriptionFeedback = $state('');
	subscriptionFeedbackError = $state(false);
	domainFeedback = $state('');
	domainFeedbackError = $state(false);
	listFeedback = $state('');
	listFeedbackError = $state(false);
	exceptionFeedback = $state('');
	exceptionFeedbackError = $state(false);
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

	async changeException(input: string, enabled: boolean): Promise<boolean> {
		if (this.changingException || !this.state?.enabled) return false;
		this.changingException = true;
		try {
			const next = await settingsBridge.setException(input.trim(), enabled);
			if (this.disposed) return false;
			++this.refreshEpoch;
			this.state = next;
			this.exceptionFeedback = t(enabled ? 'exceptionRemoved' : 'exceptionAdded');
			this.exceptionFeedbackError = false;
			return true;
		} catch (error) {
			if (!this.disposed) {
				this.exceptionFeedback = t(error === 'invalid-site' ? 'invalidSite' : 'changeFailed');
				this.exceptionFeedbackError = true;
			}
			return false;
		} finally {
			if (!this.disposed) this.changingException = false;
		}
	}

	async checkLists(): Promise<void> {
		if (this.updating || this.state?.updating || !this.state?.updatesAvailable) return;
		this.updating = true;
		this.listFeedback = t('checkingFeedback');
		this.listFeedbackError = false;
		try {
			const next = await settingsBridge.checkLists();
			if (this.disposed) return;
			++this.refreshEpoch;
			this.state = next;
			this.listFeedback = t(
				next.updateSucceeded ? (next.pendingRestart ? 'updateReady' : 'upToDate') : 'updateFailed'
			);
			this.listFeedbackError = !next.updateSucceeded;
		} catch (error) {
			if (!this.disposed) {
				this.listFeedback = t(
					error === 'updates-unavailable' ? 'updatesUnavailable' : 'updateFailed'
				);
				this.listFeedbackError = true;
			}
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

	removeDomains(domains: string[]): Promise<boolean> {
		return this.editDomain(() => settingsBridge.removeDomains(domains), 'domainsRemoved');
	}

	private async editSubscription(
		request: () => Promise<ContentBlockingState>,
		message: string
	): Promise<boolean> {
		if (this.changingSubscriptions || this.state?.updating || !this.state?.updatesAvailable)
			return false;
		this.changingSubscriptions = true;
		this.subscriptionFeedback = '';
		try {
			const next = await request();
			if (this.disposed) return false;
			++this.refreshEpoch;
			this.state = next;
			this.subscriptionFeedback = t(message);
			this.subscriptionFeedbackError = false;
			return true;
		} catch (error) {
			const keys: Record<string, string> = {
				'invalid-list-url': 'invalidListUrl',
				'duplicate-list': 'duplicateList',
				'too-many-lists': 'subscriptionLimit',
				'list-storage-limit': 'subscriptionStorageLimit',
				'invalid-list': 'invalidList',
				'download-failed': 'subscriptionDownloadFailed',
				'lists-busy': 'subscriptionBusy',
				'private-profile': 'privateUpdateRestriction',
				'updates-unavailable': 'updatesUnavailable'
			};
			if (!this.disposed) {
				this.subscriptionFeedback = t(keys[String(error)] ?? 'changeFailed');
				this.subscriptionFeedbackError = true;
			}
			return false;
		} finally {
			if (!this.disposed) this.changingSubscriptions = false;
		}
	}

	addSubscription(url: string): Promise<boolean> {
		return this.editSubscription(
			() => settingsBridge.addSubscription(url.trim()),
			'subscriptionAdded'
		);
	}
	changeSubscription(url: string, enabled: boolean): Promise<boolean> {
		return this.editSubscription(
			() => settingsBridge.setSubscriptionEnabled(url, enabled),
			'subscriptionChanged'
		);
	}
	removeSubscription(url: string): Promise<boolean> {
		return this.editSubscription(
			() => settingsBridge.removeSubscription(url),
			'subscriptionRemoved'
		);
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
