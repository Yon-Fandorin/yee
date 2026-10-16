/// <reference path="./native/management.d.ts" />

import { t } from '#lib/i18n.ts';
import { loadTimeData } from 'chrome://resources/js/load_time_data.js';
import type {} from './native/developer_private.js';

export type ExtensionInfo = chrome.developerPrivate.ExtensionInfo;
export type HostAccess = chrome.developerPrivate.HostAccess;
export type ExtensionConfiguration = Partial<
	Omit<
		chrome.developerPrivate.ExtensionConfigurationUpdate,
		'extensionId' | 'acknowledgeSafetyCheckWarningReason'
	>
>;

export function enabled(item: ExtensionInfo): boolean {
	return item.state === 'ENABLED' || item.state === 'TERMINATED';
}

export function displayState(item: ExtensionInfo): string {
	switch (item.state) {
		case 'ENABLED':
			return t('extensionsEnabled');
		case 'TERMINATED':
			return t('extensionsTerminated');
		case 'BLOCKLISTED':
			return t('extensionsBlocked');
		default:
			return t('extensionsDisabled');
	}
}

export function warningMessages(item: ExtensionInfo): string[] {
	const warnings: string[] = [];
	const reasons: [keyof chrome.developerPrivate.DisableReasons, string][] = [
		['blockedByPolicy', 'extensionsPolicyBlocked'],
		['corruptInstall', 'extensionsCorruptInstall'],
		['suspiciousInstall', 'extensionsSuspiciousInstall'],
		['updateRequired', 'extensionsUpdateRequired'],
		['publishedInStoreRequired', 'extensionsStoreRequired'],
		['unsupportedManifestVersion', 'extensionsUnsupportedManifest'],
		['unsupportedDeveloperExtension', 'extensionsUnsupportedDeveloper'],
		['custodianApprovalRequired', 'extensionsCustodianApproval'],
		['parentDisabledPermissions', 'extensionsParentPermissions'],
		['reloading', 'extensionsReloading']
	];
	if (item.blocklistText) warnings.push(item.blocklistText);
	const safety = item.safetyCheckText?.detailString || item.safetyCheckText?.panelString;
	if (safety) warnings.push(safety);
	for (const [reason, key] of reasons) {
		if (item.disableReasons[reason]) warnings.push(t(key));
	}
	if (item.dependentExtensions.length) warnings.push(t('extensionsDependencyWarning'));
	if (item.showSafeBrowsingAllowlistWarning) warnings.push(t('extensionsAllowlistWarning'));
	warnings.push(...item.runtimeWarnings, ...item.installWarnings);
	return [...new Set(warnings)];
}

/** Chromium 런타임 호스트 대화상자와 같은 사이트 입력을 허용한다. */
export function hostPattern(input: string): string | null {
	const match =
		/^((http|https|\*):\/\/)?(\*\.)?([a-z0-9.-]+\.[a-z0-9]+|localhost)(:[0-9]+)?(\/\*|\/)?$/.exec(
			input.trim().toLowerCase()
		);
	if (!match) return null;
	return `${match[1] || '*://'}${match[3] || ''}${match[4]}${match[5] || ''}/*`;
}

function cancelled(error: unknown): boolean {
	const message = error instanceof Error ? error.message : String(error);
	return (
		/^Extension .* uninstall canceled by user\.$/.test(message) ||
		message === 'The user did not accept the re-enable dialog.'
	);
}

export class ExtensionsModel {
	items = $state<ExtensionInfo[]>([]);
	query = $state('');
	loading = $state(true);
	loadFailed = $state(false);
	error = $state('');
	busy = $state('');
	profile = $state<chrome.developerPrivate.ProfileInfo | null>(null);
	private disposed = false;
	private connected = false;
	private refreshDirty = false;
	private refreshTask: Promise<void> | null = null;
	private profileVersion = 0;
	private refreshTimer: ReturnType<typeof setTimeout> | undefined;
	private unsubscribe: (() => void)[] = [];

	get supportsHostEditing(): boolean {
		// 향상된 제어는 네이티브 UI에서 확장 권한과 프로필 전체 사이트 제한을 함께 조정한다.
		return (
			!loadTimeData.getBoolean('enableEnhancedSiteControls') &&
			!loadTimeData.getBoolean('enableUserPermittedSites')
		);
	}

	get visibleItems() {
		const query = this.query.trim().toLocaleLowerCase();
		return query
			? this.items.filter((item) =>
					[item.name, item.description, item.id].some((text) =>
						text.toLocaleLowerCase().includes(query)
					)
				)
			: this.items;
	}

	setQuery(query: string) {
		this.query = query;
	}

	async connect() {
		if (this.disposed) return;
		try {
			if (typeof chrome === 'undefined' || !chrome.developerPrivate || !chrome.management) {
				throw new Error('Unavailable');
			}
			if (!this.connected) {
				// 설치나 정책 변경을 놓치지 않도록 스냅샷을 읽기 전에 구독한다.
				const changed = () => this.scheduleRefresh();
				const profileChanged = (profile: chrome.developerPrivate.ProfileInfo) => {
					if (this.disposed) return;
					this.profileVersion++;
					this.profile = profile;
					this.scheduleRefresh();
				};
				chrome.developerPrivate.onItemStateChanged.addListener(changed);
				this.unsubscribe.push(() =>
					chrome.developerPrivate.onItemStateChanged.removeListener(changed)
				);
				chrome.developerPrivate.onProfileStateChanged.addListener(profileChanged);
				this.unsubscribe.push(() =>
					chrome.developerPrivate.onProfileStateChanged.removeListener(profileChanged)
				);
				this.connected = true;
			}
			await this.refresh();
		} catch {
			this.unsubscribe.forEach((unsubscribe) => unsubscribe());
			this.unsubscribe = [];
			this.connected = false;
			this.failLoading();
		}
	}

	private scheduleRefresh() {
		if (this.disposed) return;
		this.refreshDirty = true;
		clearTimeout(this.refreshTimer);
		// 진행 중인 읽기는 후속 변경까지 반영한 뒤 호출자에게 반환한다.
		if (this.refreshTask) return;
		this.refreshTimer = setTimeout(() => void this.refresh(), 50);
	}

	refresh(): Promise<void> {
		if (this.disposed) return Promise.resolve();
		clearTimeout(this.refreshTimer);
		this.refreshDirty = true;
		if (!this.refreshTask) {
			// 읽기가 끝나기 전에 공유 Promise가 할당되도록 실행을 미룬다.
			this.refreshTask = Promise.resolve().then(() => this.drainRefresh());
		}
		return this.refreshTask;
	}

	private async drainRefresh() {
		try {
			while (this.refreshDirty && !this.disposed) {
				this.refreshDirty = false;
				const profileVersion = this.profileVersion;
				try {
					const [items, profile] = await Promise.all([
						chrome.developerPrivate.getExtensionsInfo({
							includeDisabled: true,
							includeTerminated: true
						}),
						chrome.developerPrivate.getProfileConfiguration()
					]);
					if (this.disposed) return;
					if (this.refreshDirty) continue;
					this.items = [...items].sort(
						(a, b) => a.name.localeCompare(b.name) || a.id.localeCompare(b.id)
					);
					if (profileVersion === this.profileVersion) this.profile = profile;
					if (this.loadFailed) this.error = '';
					this.loading = false;
					this.loadFailed = false;
				} catch {
					if (this.disposed) return;
					if (this.refreshDirty) continue;
					this.failLoading();
				}
			}
		} finally {
			// 호출자에게 반환하기 전에 공유 Promise를 비운다.
			// 변경 작업은 최신 스냅샷을 반영할 때까지 busy 상태를 유지한다.
			this.refreshTask = null;
		}
	}

	canToggle(item: ExtensionInfo): boolean {
		const reasons = item.disableReasons;
		return (
			item.userMayModify &&
			item.state !== 'BLOCKLISTED' &&
			item.state !== 'TERMINATED' &&
			!item.dependentExtensions.length &&
			!reasons.corruptInstall &&
			!reasons.suspiciousInstall &&
			!reasons.updateRequired &&
			!reasons.publishedInStoreRequired &&
			!reasons.blockedByPolicy &&
			!reasons.unsupportedManifestVersion &&
			!reasons.unsupportedDeveloperExtension &&
			!reasons.reloading
		);
	}

	async setEnabled(item: ExtensionInfo, value: boolean) {
		return this.perform(item, async (current) => {
			if (!this.canToggle(current)) throw new Error('Unavailable');
			await chrome.management.setEnabled(current.id, value);
		});
	}

	async remove(item: ExtensionInfo) {
		return this.perform(item, async (current) => {
			if (current.mustRemainInstalled) throw new Error('Unavailable');
			await chrome.management.uninstall(current.id, { showConfirmDialog: true });
		});
	}

	async setConfiguration(item: ExtensionInfo, config: ExtensionConfiguration) {
		return this.perform(item, async (current) => {
			const update: chrome.developerPrivate.ExtensionConfigurationUpdate = {
				extensionId: current.id
			};
			const modifiers = [
				'fileAccess',
				'incognitoAccess',
				'userScriptsAccess',
				'errorCollection'
			] as const;
			for (const key of modifiers) {
				if (config[key] === undefined) continue;
				if (!current[key]?.isEnabled || typeof config[key] !== 'boolean') {
					throw new Error('Unavailable');
				}
				if (key === 'incognitoAccess' && !this.profile?.isIncognitoAvailable) {
					throw new Error('Unavailable');
				}
				update[key] = config[key];
			}
			if (config.pinnedToToolbar !== undefined) {
				if (current.pinnedToToolbar === undefined || typeof config.pinnedToToolbar !== 'boolean') {
					throw new Error('Unavailable');
				}
				update.pinnedToToolbar = config.pinnedToToolbar;
			}
			if (config.showAccessRequestsInToolbar !== undefined) {
				if (
					!this.supportsHostEditing ||
					!current.permissions.runtimeHostPermissions ||
					typeof config.showAccessRequestsInToolbar !== 'boolean'
				) {
					throw new Error('Unavailable');
				}
				update.showAccessRequestsInToolbar = config.showAccessRequestsInToolbar;
			}
			if (config.hostAccess !== undefined) {
				this.validateHostAccess(current, config.hostAccess);
				update.hostAccess = config.hostAccess;
			}
			await chrome.developerPrivate.updateExtensionConfiguration(update);
		});
	}

	private validateHostAccess(item: ExtensionInfo, access: HostAccess) {
		const permissions = item.permissions.runtimeHostPermissions;
		if (
			!this.supportsHostEditing ||
			!permissions ||
			!['ON_CLICK', 'ON_SPECIFIC_SITES', 'ON_ALL_SITES'].includes(access)
		) {
			throw new Error('Unavailable');
		}
		if (
			permissions.hasAllHosts &&
			access === 'ON_SPECIFIC_SITES' &&
			permissions.hostAccess !== 'ON_SPECIFIC_SITES'
		) {
			throw new Error(t('extensionsAddHostFirst'));
		}
	}

	async setHostAccess(item: ExtensionInfo, access: HostAccess) {
		return this.setConfiguration(item, { hostAccess: access });
	}

	async addHost(item: ExtensionInfo, input: string) {
		return this.perform(item, async (current) => {
			const permissions = current.permissions.runtimeHostPermissions;
			if (
				!this.supportsHostEditing ||
				!permissions ||
				(!permissions.hasAllHosts && permissions.hostAccess === 'ON_ALL_SITES')
			) {
				throw new Error('Unavailable');
			}
			// 유한 호스트 목록에는 네이티브 패턴이 있으므로 요청한 호스트만 허용한다.
			const pattern = permissions.hasAllHosts ? hostPattern(input) : input;
			if (!pattern) throw new Error(t('extensionsInvalidHost'));
			if (!permissions.hasAllHosts && !permissions.hosts.some((host) => host.host === pattern)) {
				throw new Error('Unavailable');
			}
			if (permissions.hasAllHosts && permissions.hostAccess !== 'ON_SPECIFIC_SITES') {
				await chrome.developerPrivate.updateExtensionConfiguration({
					extensionId: current.id,
					hostAccess: 'ON_SPECIFIC_SITES' as HostAccess
				});
			}
			await chrome.developerPrivate.addHostPermission(current.id, pattern);
		});
	}

	async removeHost(item: ExtensionInfo, host: string) {
		return this.perform(item, async (current) => {
			const permissions = current.permissions.runtimeHostPermissions;
			if (
				!this.supportsHostEditing ||
				!permissions ||
				(!permissions.hasAllHosts && permissions.hostAccess === 'ON_ALL_SITES') ||
				!permissions.hosts.some((entry) => entry.host === host && entry.granted)
			) {
				throw new Error('Unavailable');
			}
			await chrome.developerPrivate.removeHostPermission(current.id, host);
		});
	}

	async openOptions(item: ExtensionInfo) {
		return this.perform(item, async (current) => {
			if (!current.optionsPage || current.state !== 'ENABLED') throw new Error('Unavailable');
			// 탭 옵션과 네이티브 내장 옵션 대화상자는 Chromium이 소유한다.
			await chrome.developerPrivate.showOptions(current.id);
		});
	}

	nativeURL(item?: ExtensionInfo, subpath?: 'errors' | 'options' | 'shortcuts'): string {
		if (subpath === 'shortcuts') return 'chrome://extensions/shortcuts';
		if (!item) return 'chrome://extensions/';
		return `chrome://extensions/?${subpath || 'id'}=${encodeURIComponent(item.id)}`;
	}

	private async perform(item: ExtensionInfo, action: (current: ExtensionInfo) => Promise<void>) {
		if (this.disposed || this.busy || this.loading || this.loadFailed) return false;
		const current = this.items.find((entry) => entry.id === item.id);
		if (!current) return false;
		this.busy = current.id;
		this.error = '';
		let success = false;
		let failure = '';
		try {
			await action(current);
			success = true;
		} catch (error) {
			if (!cancelled(error)) {
				const message = error instanceof Error ? error.message : '';
				failure = [t('extensionsInvalidHost'), t('extensionsAddHostFirst')].includes(message)
					? message
					: t('extensionsActionFailed');
			}
		} finally {
			if (!this.disposed) {
				// 네이티브 확인, 일부 권한 갱신, 취소 모두 스냅샷을 바꿀 수 있다.
				await this.refresh();
				if (!this.disposed) {
					if (failure && !this.loadFailed) this.error = failure;
					this.busy = '';
				}
			}
		}
		return success;
	}

	private failLoading() {
		if (this.disposed) return;
		this.loading = false;
		this.loadFailed = true;
		this.error = t('extensionsLoadFailed');
	}

	dispose() {
		this.disposed = true;
		this.refreshDirty = false;
		this.profileVersion++;
		clearTimeout(this.refreshTimer);
		this.unsubscribe.forEach((unsubscribe) => unsubscribe());
		this.unsubscribe = [];
	}
}
