<script lang="ts" module>
	import Settings from '@lucide/svelte/icons/settings';
	import ShieldCheck from '@lucide/svelte/icons/shield-check';
	import ChevronRight from '@lucide/svelte/icons/chevron-right';
	import SquareArrowOutUpRight from '@lucide/svelte/icons/square-arrow-out-up-right';
	import LockKeyhole from '@lucide/svelte/icons/lock-keyhole';
	import Download from '@lucide/svelte/icons/download';
	import Monitor from '@lucide/svelte/icons/monitor';
	import Globe from '@lucide/svelte/icons/globe';

	const icons = {
		settings: Settings,
		shield: ShieldCheck,
		arrow: ChevronRight,
		external: SquareArrowOutUpRight,
		lock: LockKeyhole,
		download: Download,
		appearance: Monitor,
		globe: Globe
	};
	export type IconName = keyof typeof icons;
</script>

<script lang="ts">
	let { name, size = 16 }: { name: IconName; size?: number } = $props();
	const Glyph = $derived(icons[name]);
</script>

<Glyph {size} strokeWidth={1.6} class="shrink-0" aria-hidden="true" />
