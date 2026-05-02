import type { User } from "firebase/auth";
import { useEffect, useState } from "react";
import {
	isAuthConfigured,
	signInWithGoogle,
	signOutUser,
	subscribeAuth,
} from "../services/auth";
import u from "../styles/shared/uiPrimitives.module.css";
import um from "./UserMenu.module.css";

export function UserMenu({ onError }: { onError?: (msg: string) => void }) {
	const [user, setUser] = useState<User | null>(null);
	const [open, setOpen] = useState(false);

	useEffect(() => {
		return subscribeAuth(setUser);
	}, []);

	if (!isAuthConfigured()) {
		return null;
	}

	return (
		<div className={um.userMenu}>
			{!user ? (
				<button
					type="button"
					className={`${u.openBtn} ${um.userMenuSignin}`}
					onClick={async () => {
						try {
							await signInWithGoogle();
						} catch (e) {
							if (onError) {
								onError(e instanceof Error ? e.message : "Sign in failed");
							}
						}
					}}
				>
					Sign in
				</button>
			) : (
				<div className={um.userMenuWrap}>
					<button
						type="button"
						className={um.avatar}
						aria-label="Account"
						aria-haspopup="menu"
						aria-expanded={open}
						onClick={() => setOpen((o) => !o)}
					>
						{user.photoURL ? (
							<img
								src={user.photoURL}
								referrerPolicy="no-referrer"
								width={24}
								height={24}
								alt=""
							/>
						) : (
							<span className={um.avatarFallback} aria-hidden>
								{(user.displayName?.[0] ?? user.email?.[0])?.toUpperCase() ??
									"?"}
							</span>
						)}
					</button>
					{open && (
						// biome-ignore lint/a11y/noStaticElementInteractions: intentional transparent backdrop to close menu on outside click
						<div
							className={um.userMenuBackdrop}
							role="presentation"
							onClick={() => setOpen(false)}
						/>
					)}
					{open && (
						<div className={um.userMenuPopover} role="menu">
							<div className={um.userMenuEmail}>{user.email}</div>
							<button
								type="button"
								role="menuitem"
								className={um.userMenuOut}
								onClick={async () => {
									setOpen(false);
									await signOutUser();
								}}
							>
								Sign out
							</button>
						</div>
					)}
				</div>
			)}
		</div>
	);
}
