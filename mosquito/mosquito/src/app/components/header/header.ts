import { Component } from '@angular/core';
import { Mode } from '../mode/mode';

// @ts-ignore
import packageJson from '../../../../package.json';

@Component({
  selector: 'app-header',
  imports: [ Mode ],
  templateUrl: './header.html',
  styleUrl: './header.scss'
})
export class Header {
  version = packageJson.version;
}
