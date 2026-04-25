import { initializeApp, type FirebaseApp } from "firebase/app";
import {
	GoogleAuthProvider,
	getAuth,
	type User,
	onAuthStateChanged,
	signInWithPopup,
	signOut,
} from "firebase/auth";

let app: FirebaseApp | null = null;

export function isAuthConfigured(): boolean {
	return Boolean(
		import.meta.env.VITE_FIREBASE_API_KEY &&
			import.meta.env.VITE_FIREBASE_AUTH_DOMAIN &&
			import.meta.env.VITE_FIREBASE_PROJECT_ID,
	);
}

function getOrInitApp() {
	if (app) return app;
	const apiKey = import.meta.env.VITE_FIREBASE_API_KEY;
	const authDomain = import.meta.env.VITE_FIREBASE_AUTH_DOMAIN;
	const projectId = import.meta.env.VITE_FIREBASE_PROJECT_ID;
	if (!apiKey || !authDomain || !projectId) {
		throw new Error(
			"Missing Firebase config: set VITE_FIREBASE_API_KEY, VITE_FIREBASE_AUTH_DOMAIN, VITE_FIREBASE_PROJECT_ID",
		);
	}
	app = initializeApp({ apiKey, authDomain, projectId });
	return app;
}

export function getAuthInstance() {
	return getAuth(getOrInitApp());
}

export function subscribeAuth(fn: (u: User | null) => void) {
	if (!isAuthConfigured()) {
		fn(null);
		return () => {
			/* no-op */
		};
	}
	return onAuthStateChanged(getAuthInstance(), fn);
}

export function getCurrentUser(): User | null {
	return getAuthInstance().currentUser;
}

export async function signInWithGoogle() {
	if (!isAuthConfigured()) {
		throw new Error(
			"Firebase is not configured. Add VITE_FIREBASE_* to your .env (see .env.example).",
		);
	}
	const auth = getAuthInstance();
	await signInWithPopup(auth, new GoogleAuthProvider());
}

export async function signOutUser() {
	await signOut(getAuthInstance());
}

export async function getIdToken(forceRefresh = false): Promise<string> {
	const u = getAuthInstance().currentUser;
	if (!u) throw new Error("Not signed in");
	return await u.getIdToken(forceRefresh);
}
