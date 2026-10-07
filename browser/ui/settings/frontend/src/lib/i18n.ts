import { loadTimeData } from 'chrome://resources/js/load_time_data.js';
import 'chrome://yee-settings/strings.m.js';

export const productName = loadTimeData.getString('productName');
export const locale = loadTimeData.getString('applicationLocale');
export const nativeSettingsUrl = loadTimeData.getString('nativeSettingsUrl');
export const domainImportMaxBytes = loadTimeData.getInteger('domainImportMaxBytes');
export function t(key: string, ...args: string[]): string {
	return args.length ? loadTimeData.getStringF(key, ...args) : loadTimeData.getString(key);
}
