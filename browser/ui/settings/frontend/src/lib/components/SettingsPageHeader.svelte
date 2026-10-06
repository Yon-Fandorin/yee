<script lang="ts">
	let { titleId, title, description }: { titleId: string; title: string; description: string } =
		$props();
</script>

<header class="page-header">
	<h1 id={titleId}>{title}</h1>
	<p>{description}</p>
</header>

<style>
	@layer components {
		.page-header {
			margin-bottom: 34px;
		}
		h1 {
			margin: 0 0 7px;
			font-size: 22px;
			font-weight: 550;
			letter-spacing: -0.6px;
			line-height: 1.4;
		}
		p {
			color: var(--muted-foreground);
			font-size: 13px;
			max-width: 65ch;
		}
		@media (max-width: 540px) {
			.page-header {
				margin-bottom: 26px;
			}
			h1 {
				font-size: 21px;
			}
		}
	}
</style>
