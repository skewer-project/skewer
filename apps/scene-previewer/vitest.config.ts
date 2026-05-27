import { defineConfig } from "vitest/config";

export default defineConfig({
	test: {
		include: ["test/services/**/*.test.ts"],
	},
});
