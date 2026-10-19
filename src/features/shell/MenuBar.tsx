import React, { useEffect, useRef, useState } from 'react';

export interface MenuItem {
  label?: string;
  shortcut?: string;
  onSelect?: () => void;
  disabled?: boolean;
  checked?: boolean;
  separator?: boolean;
  /** Items sharing a heading are grouped under it. */
  group?: string;
}

export interface MenuDefinition {
  label: string;
  items: MenuItem[];
}

interface Props {
  menus: MenuDefinition[];
  /** Right-aligned status text, e.g. the active model and workspace. */
  status?: React.ReactNode;
}

/**
 * Top-bar menus.
 *
 * Every entry runs a real action — nothing here is decorative. Escape and
 * outside clicks close the open menu; arrow keys move between menus while one is
 * open.
 */
export default function MenuBar({ menus, status }: Props) {
  const [openIndex, setOpenIndex] = useState<number | null>(null);
  const barRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (openIndex === null) return;
    const onPointerDown = (event: MouseEvent) => {
      if (barRef.current && !barRef.current.contains(event.target as Node)) setOpenIndex(null);
    };
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') setOpenIndex(null);
      if (event.key === 'ArrowRight') setOpenIndex(index => (index === null ? null : (index + 1) % menus.length));
      if (event.key === 'ArrowLeft') setOpenIndex(index => (index === null ? null : (index - 1 + menus.length) % menus.length));
    };
    document.addEventListener('mousedown', onPointerDown);
    document.addEventListener('keydown', onKeyDown);
    return () => {
      document.removeEventListener('mousedown', onPointerDown);
      document.removeEventListener('keydown', onKeyDown);
    };
  }, [openIndex, menus.length]);

  return (
    <div className="codex-menubar" ref={barRef}>
      <span className="codex-menubar-brand">Consiglio</span>
      <div className="codex-menubar-items">
        {menus.map((menu, index) => (
          <div className="codex-menu" key={menu.label}>
            <button
              type="button"
              className={`codex-menu-button${openIndex === index ? ' is-open' : ''}`}
              aria-haspopup="menu"
              aria-expanded={openIndex === index}
              onClick={() => setOpenIndex(current => (current === index ? null : index))}
            >
              {menu.label}
            </button>
            {openIndex === index && (
              <div className="codex-menu-popup" role="menu">
                {menu.items.map((item, itemIndex) => {
                  if (item.separator) return <div className="codex-menu-separator" key={`sep-${itemIndex}`} />;
                  const heading = item.group && (itemIndex === 0 || menu.items[itemIndex - 1]?.group !== item.group);
                  return (
                    <React.Fragment key={`${item.label}-${itemIndex}`}>
                      {heading && <div className="codex-menu-group">{item.group}</div>}
                      <button
                        type="button"
                        role="menuitem"
                        className="codex-menu-item"
                        disabled={item.disabled}
                        onClick={() => {
                          setOpenIndex(null);
                          item.onSelect?.();
                        }}
                      >
                        <span className="codex-menu-check" aria-hidden="true">{item.checked ? '✓' : ''}</span>
                        <span className="codex-menu-label">{item.label}</span>
                        {item.shortcut && <span className="codex-menu-shortcut">{item.shortcut}</span>}
                      </button>
                    </React.Fragment>
                  );
                })}
              </div>
            )}
          </div>
        ))}
      </div>
      <div className="codex-menubar-status">{status}</div>
    </div>
  );
}
