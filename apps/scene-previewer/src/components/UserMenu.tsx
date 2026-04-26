import type { User } from "firebase/auth";
import { useEffect, useState } from "react";
import {
	isAuthConfigured,
	signInWithGoogle,
	signOutUser,
	subscribeAuth,
} from "../services/auth";

export function UserMenu({ onError }: { onError?: (msg: string) => void }) {
	const [user, setUser] = useState<User | null>(null);
	const [open, setOpen] = useState(false);
	const [photoFailed, setPhotoFailed] = useState(false);

	useEffect(() => {
		return subscribeAuth(setUser);
	}, []);

	useEffect(() => {
		setPhotoFailed(false);
	}, [user]);

	if (!isAuthConfigured()) {
		return null;
	}

	return (
		<div className="user-menu">
			{!user ? (
				<button
					type="button"
					className="open-btn user-menu-signin"
					onClick={async () => {
						try {
							await signInWithGoogle();
						} catch (e) {
							onError?.(e instanceof Error ? e.message : "Sign in failed");
						}
					}}
				>
					Sign in
				</button>
			) : (
				<div className="user-menu-wrap">
					<button
						type="button"
						className="avatar"
						aria-label="Account"
						aria-haspopup="menu"
						aria-expanded={open}
						onClick={() => setOpen((o) => !o)}
					>
						{user.photoURL && !photoFailed ? (
							<img
								src={user.photoURL}
								width={24}
								height={24}
								alt=""
								referrerPolicy="no-referrer"
								onError={() => setPhotoFailed(true)}
							/>
						) : (
							<span className="avatar-fallback" aria-hidden>
								{(user.displayName?.[0] ?? user.email?.[0])?.toUpperCase() ??
									"?"}
							</span>
						)}
					</button>
					{open && (
						// biome-ignore lint/a11y/noStaticElementInteractions: intentional transparent backdrop to close menu on outside click
						<div
							className="user-menu-backdrop"
							role="presentation"
							onClick={() => setOpen(false)}
						/>
					)}
					{open && (
						<div className="user-menu-popover" role="menu">
							<div className="user-menu-email">{user.email}</div>
							<button
								type="button"
								role="menuitem"
								className="user-menu-out"
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
