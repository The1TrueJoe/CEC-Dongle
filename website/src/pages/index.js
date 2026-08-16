import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import Heading from '@theme/Heading';
import styles from './index.module.css';

function HomepageHeader() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <header className={clsx('hero hero--primary', styles.heroBanner)}>
      <div className="container">
        <Heading as="h1" className="hero__title">
          {siteConfig.title}
        </Heading>
        <p className="hero__subtitle">{siteConfig.tagline}</p>
        <div className={styles.buttons}>
          <Link className="button button--secondary button--lg" to="/docs/getting-started">
            Get Started
          </Link>
          <Link className="button button--outline button--secondary button--lg" to="/api-reference">
            API Reference
          </Link>
        </div>
      </div>
    </header>
  );
}

const features = [
  {
    title: 'REST API',
    description:
      'Send raw CEC frames or use named commands — tv_on, volume_up, input2 — that resolve destinations from device config. No opcodes to memorize.',
  },
  {
    title: 'Zero-config discovery',
    description:
      'Advertises over mDNS and SDDP the moment it joins WiFi. No static IPs, no manual configuration on the client side.',
  },
  {
    title: 'Control4 driver included',
    description:
      'Presents as a TV proxy with real-time state over a persistent TCP push connection — power, volume, input, and mute changes arrive instantly, not on a poll.',
  },
];

function Feature({title, description}) {
  return (
    <div className={clsx('col col--4')}>
      <div className="padding-horiz--md">
        <Heading as="h3">{title}</Heading>
        <p>{description}</p>
      </div>
    </div>
  );
}

export default function Home() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <Layout
      title={siteConfig.title}
      description="REST-controlled HDMI-CEC bridge firmware for the SMLIGHT SLWF-08">
      <HomepageHeader />
      <main>
        <section className={styles.features}>
          <div className="container">
            <div className="row">
              {features.map((props, idx) => (
                <Feature key={idx} {...props} />
              ))}
            </div>
          </div>
        </section>
      </main>
    </Layout>
  );
}
